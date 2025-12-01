#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>

#include "frontend/frontend.h"
#include "base/exception.h"
#include "pim_codegen/codegen.h"
#include "pim_codegen/kv_cache_policy.h"
#include "pim_codegen/static_weight_loader.h"
#include "memory_system/bank_conflict_tracker.h"
#include "frontend/impl/memory_trace/kv_cache_trace_generator.h"
#include "dram/dram.h"

namespace Ramulator {

namespace fs = std::filesystem;

/**
 * @brief Enhanced PIM trace frontend with KV cache awareness
 * 
 * This frontend extends the base PimTrace to support KV cache placement policies
 * and bank conflict tracking for LLM inference workloads.
 */
class PimTraceKVAware : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, PimTraceKVAware, "PimTraceKVAware", 
                                    "PIM trace with KV cache placement policy support.")

  private:
    std::vector<PIM::Trace> m_trace;
    std::vector<std::vector<std::vector<std::string>>> m_kernels;

    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    Logger_t m_logger;

    PIM::IPimCodeGen* m_pim_codegen;
    PIM::IKVCachePolicy* m_kv_cache_policy;
    BankConflictTracker* m_conflict_tracker;
    PIM::KVCacheTraceGenerator* m_kv_trace_generator;

    // Configuration
    std::string m_static_weight_trace_path;
    bool m_enable_kv_cache;
    int m_num_tokens;
    int m_num_banks;

  public:
    void init() override {
      std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
      m_clock_ratio = param<uint>("clock_ratio").required();
      
      // KV cache configuration
      m_enable_kv_cache = param<bool>("enable_kv_cache").default_val(false);
      m_static_weight_trace_path = param<std::string>("static_weight_trace_path").default_val("");
      m_num_tokens = param<int>("num_tokens").default_val(512);
      
      m_pim_codegen = create_child_ifce<PIM::IPimCodeGen>();
      
      // Create KV cache policy if enabled
      if (m_enable_kv_cache) {
        m_kv_cache_policy = create_child_ifce<PIM::IKVCachePolicy>();
      } else {
        m_kv_cache_policy = nullptr;
      }

      m_logger = Logging::create_logger("PIM Trace KV-Aware");
      m_logger->info("Loading trace file {} ...", trace_path_str);
      init_trace(trace_path_str);
      m_logger->info("Loaded {} lines.", m_trace.size());      
    };

    void connect_memory_system(IMemorySystem* memory_system) override {
      IFrontEnd::connect_memory_system(memory_system);
      
      // Initialize KV cache policy with static weight mapping
      if (m_enable_kv_cache && m_kv_cache_policy) {
        IDRAM* dram = memory_system->get_ifce<IDRAM>();
        m_num_banks = dram->get_level_size("bank") * 
                      dram->get_level_size("bankgroup") *
                      dram->get_level_size("channel");
        
        // Load static weight mapping from OptiPIM trace
        std::map<int, std::unordered_set<uint64_t>> static_weight_mapping;
        if (!m_static_weight_trace_path.empty()) {
          static_weight_mapping = PIM::StaticWeightLoader::extract_weight_banks(
              m_static_weight_trace_path, m_num_banks);
          m_logger->info("Loaded static weight mapping from {}", m_static_weight_trace_path);
        }
        
        // Initialize KV cache policy
        m_kv_cache_policy->init(dram, m_num_banks, static_weight_mapping);
        
        // Initialize conflict tracker
        m_conflict_tracker = new BankConflictTracker(m_num_banks);
        
        // Initialize KV trace generator
        m_kv_trace_generator = new PIM::KVCacheTraceGenerator(
            m_kv_cache_policy, m_conflict_tracker, dram, m_num_banks);
        
        m_logger->info("KV cache policy initialized with {} banks", m_num_banks);
      }
      
      expand_trace();
    };

    void tick() override {
      if (m_curr_trace_idx > m_trace_length - 1) return;
      const PIM::Trace& t = m_trace[m_curr_trace_idx];
      
      // Track conflicts if KV cache is enabled
      if (m_enable_kv_cache && m_conflict_tracker && t.addr_vec.size() >= 2) {
        int bank_id = t.addr_vec[1];
        uint64_t addr = 0;
        for (size_t i = 0; i < t.addr_vec.size() && i < 4; i++) {
          addr = (addr << 16) | (t.addr_vec[i] & 0xFFFF);
        }
        
        // Determine if this is a weight or KV cache operation
        // For now, we'll use heuristics based on operation type
        if (t.op == "write" || t.op == "compute") {
          // Could be weight operation
          m_conflict_tracker->register_weight_operation(bank_id, addr, m_curr_trace_idx);
        } else if (t.op == "read") {
          // Could be KV cache read
          // We'd need metadata to distinguish, but for now track both
        }
      }
      
      bool trace_sent = m_memory_system->send({t.addr_vec, t.op});
      if (trace_sent) {
        m_curr_trace_idx = (m_curr_trace_idx + 1);
        if (m_curr_trace_idx % 100000000 == 0) {
          m_logger->info("Finished - {} / {} traces.", m_curr_trace_idx, m_trace.size());
        }
      }
    };

  private:
    void expand_trace() {
      std::vector<PIM::Trace> old_trace;
      for (auto t : m_trace) {
        old_trace.push_back(t);
      }
      m_trace.clear();
      m_trace.shrink_to_fit();
      int total_traces = 0;
      
      // If KV cache is enabled, inject KV cache operations
      if (m_enable_kv_cache && m_kv_trace_generator) {
        // Generate KV cache operations for each token
        for (int token_id = 0; token_id < m_num_tokens; token_id++) {
          auto kv_traces = m_kv_trace_generator->generate_inference_step(token_id);
          for (const auto& [op, addr_vec] : kv_traces) {
            m_trace.push_back({op, addr_vec});
            total_traces++;
          }
        }
        m_logger->info("Generated {} KV cache operations for {} tokens", 
                       total_traces, m_num_tokens);
      }
      
      // Process original trace
      for (auto t : old_trace) {
        if (t.op != "kernel") {
          m_trace.push_back(t);
          total_traces++;
        } else {
          std::vector<PIM::Trace> kernel_trace;
          m_pim_codegen->codegen_kernel(m_kernels[t.addr_vec[0]], kernel_trace);
          m_logger->info("Kernel {}, #Insts: {}.", m_kernels[t.addr_vec[0]][0][0], kernel_trace.size());
          total_traces += kernel_trace.size();
          for (auto kt : kernel_trace) {
            m_trace.push_back(kt);
            if (m_trace.size() > 10000000) { 
              break;
            }
          }
          kernel_trace.clear();
          kernel_trace.shrink_to_fit();
        }
      }
      
      m_trace_length = m_trace.size();
      m_logger->info("After kernel expansion - {} / {} lines.", m_trace.size(), total_traces);   
    }

    void init_trace(const std::string& file_path_str) {
      fs::path trace_path(file_path_str);
      if (!fs::exists(trace_path)) {
        throw ConfigurationError("Trace {} does not exist!", file_path_str);
      }

      std::ifstream trace_file(trace_path);
      if (!trace_file.is_open()) {
        throw ConfigurationError("Trace {} cannot be opened!", file_path_str);
      }

      std::string kernel_cmd = "";
      std::vector<std::vector<std::string>> tokens_list;

      std::string line;
      while (std::getline(trace_file, line)) {
        std::vector<std::string> tokens;
        tokenize(tokens, line, " ");
        if (line.empty()) {
          continue;
        }

        std::string op = ""; 
        if (tokens[0] == "R") {
          op = "read";
        } else if (tokens[0] == "W") {
          op = "write";
        } else if (tokens[0] == "C") {
          op = "compute";
        } else if (tokens[0] == "SR") {
          op = "subarray-read";
        } else if (tokens[0] == "SW") {
          op = "subarray-write";
        } else if (tokens[0] == "BR") {
          op = "bank-read";
        } else if (tokens[0] == "BW") {
          op = "bank-write";
        } else if (tokens[0] == "conv2d" || tokens[0] == "gemm") {
          op = "kernel_start";
          kernel_cmd = tokens[0];
          tokens_list.clear();
          tokens.push_back("");
          tokens_list.push_back(tokens);
        } else if (tokens[0] == "end") {
          op = "kernel_end";
          m_kernels.push_back(tokens_list);
          kernel_cmd = "";
        } else if (kernel_cmd != "") {
          op = "kernel_desc";
          tokens_list.push_back(tokens);
        } else {
          throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        if (op.find("kernel") == -1) {
          std::vector<std::string> addr_vec_tokens;
          tokenize(addr_vec_tokens, tokens[1], ",");

          AddrVec_t addr_vec;
          for (const auto& token : addr_vec_tokens) {
            addr_vec.push_back(std::stoll(token));
          }

          m_trace.push_back({op, addr_vec});
        } else if (op == "kernel_end") {
          op = "kernel";
          AddrVec_t addr_vec;
          addr_vec.push_back(m_kernels.size() - 1);
          m_trace.push_back({op, addr_vec});
        }
      }

      trace_file.close();
    };

    bool is_finished() override {
      return m_memory_system->finished();
    };
    
    void finalize() override {
      if (m_enable_kv_cache && m_kv_cache_policy && m_conflict_tracker) {
        auto policy_stats = m_kv_cache_policy->get_stats();
        auto conflict_stats = m_conflict_tracker->get_stats();
        
        m_logger->info("KV Cache Policy Statistics:");
        for (const auto& [name, value] : policy_stats) {
          m_logger->info("  {}: {}", name, value);
        }
        
        m_logger->info("Bank Conflict Statistics:");
        for (const auto& [name, value] : conflict_stats) {
          m_logger->info("  {}: {}", name, value);
        }
      }
    }
};

}  // namespace Ramulator

