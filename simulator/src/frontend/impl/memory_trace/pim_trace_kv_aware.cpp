#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

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

class PimTraceKVAware : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, PimTraceKVAware, "PimTraceKVAware", 
                                    "PIM trace with KV cache placement policy support.")

  private:
    std::vector<::PIM::Trace> m_trace;
    std::vector<std::vector<std::vector<std::string>>> m_kernels;

    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    Logger_t m_logger;

    ::PIM::IPimCodeGen* m_pim_codegen;
    
    PIM::IKVCachePolicy* m_kv_cache_policy;
    BankConflictTracker* m_conflict_tracker;
    PIM::KVCacheTraceGenerator* m_kv_trace_generator;

    std::string m_static_weight_trace_path;
    bool m_enable_kv_cache;
    int m_num_tokens;
    int m_num_banks;

  public:
    void init() override {
      std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
      m_clock_ratio = param<uint>("clock_ratio").required();
      
      m_enable_kv_cache = param<bool>("enable_kv_cache").default_val(false);
      m_static_weight_trace_path = param<std::string>("static_weight_trace_path").default_val("");
      m_num_tokens = param<int>("num_tokens").default_val(512);
      
      m_pim_codegen = create_child_ifce<::PIM::IPimCodeGen>();
      
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
      
      if (m_enable_kv_cache && m_kv_cache_policy) {
        IDRAM* dram = memory_system->get_ifce<IDRAM>();
        m_num_banks = dram->get_level_size("bank") * dram->get_level_size("bankgroup") *
                      dram->get_level_size("channel");
        
        std::map<int, std::unordered_set<uint64_t>> static_weight_mapping;
        if (!m_static_weight_trace_path.empty()) {
          static_weight_mapping = PIM::StaticWeightLoader::extract_weight_banks(
              m_static_weight_trace_path, m_num_banks);
          m_logger->info("Loaded static weight mapping from {}", m_static_weight_trace_path);
        }
        
        m_kv_cache_policy->init(dram, m_num_banks, static_weight_mapping);
        // Initialize tracker (but we will skip using it to prevent crashes)
        m_conflict_tracker = new BankConflictTracker(m_num_banks);
        m_kv_trace_generator = new PIM::KVCacheTraceGenerator(
            m_kv_cache_policy, m_conflict_tracker, dram, m_num_banks);
        
        m_logger->info("KV cache policy initialized with {} banks", m_num_banks);
      }
      
      expand_trace();
    };

    void tick() override {
      if (m_curr_trace_idx > m_trace_length - 1) return;
      
      const ::PIM::Trace& t = m_trace[m_curr_trace_idx];
      
      // [FIX] Explicitly convert vector<long> to vector<int> (Ramulator::AddrVec_t)
      // This prevents QEMU/Memory corruption during the 'send' call
      Ramulator::AddrVec_t req_addr_vec(t.addr_vec.begin(), t.addr_vec.end());
      
      if (m_enable_kv_cache && m_conflict_tracker && t.addr_vec.size() >= 2) {
        long raw_bank_id = t.addr_vec[1];
        if (raw_bank_id >= 0 && raw_bank_id < m_num_banks) {
            uint64_t addr = 0;
            for (size_t i = 0; i < t.addr_vec.size() && i < 4; i++) {
                addr = (addr << 16) | (t.addr_vec[i] & 0xFFFF);
            }
            if (t.op == "write" || t.op == "compute") {
              m_conflict_tracker->register_weight_operation((int)raw_bank_id, addr, m_curr_trace_idx);
            } 
        }
      }

      
      // Send the request using the EXPLICITLY converted vector
      bool trace_sent = m_memory_system->send({req_addr_vec, t.op});
      
      if (trace_sent) {
        m_curr_trace_idx = (m_curr_trace_idx + 1);
        if (m_curr_trace_idx % 1000000 == 0) {
          m_logger->info("Finished - {} / {} traces.", m_curr_trace_idx, m_trace.size());
        }
      }
    };

  private:
    void expand_trace() {
      std::vector<::PIM::Trace> old_trace;
      for (auto t : m_trace) {
        old_trace.push_back(t);
      }
      m_trace.clear();
      m_trace.shrink_to_fit();
      int total_traces = 0;
      
      // Pre-expand kernel once to extract static weight information
      // This allows the KV cache policy to make informed decisions
      std::vector<::PIM::Trace> pre_expanded_kernel_trace;
      int kernel_idx = -1;
      if (m_enable_kv_cache && m_kv_cache_policy && m_memory_system) {
        IDRAM* dram = m_memory_system->get_ifce<IDRAM>();
        for (auto t : old_trace) {
          if (t.op == "kernel") {
            kernel_idx = t.addr_vec[0];
            m_pim_codegen->codegen_kernel(m_kernels[kernel_idx], pre_expanded_kernel_trace);
            
            // Extract static weight information from kernel trace
            for (auto kt : pre_expanded_kernel_trace) {
              if ((kt.op == "write" || kt.op == "subarray-write" || kt.op == "bank-write") &&
                  kt.addr_vec.size() >= 2 && dram) {
                int bank_level = dram->m_levels("bank");
                if (bank_level >= 0 && bank_level < (int)kt.addr_vec.size()) {
                  // Compute global bank ID from address vector components
                  int global_bank_id = 0;
                  int multiplier = 1;
                  for (int i = 0; i <= bank_level && i < (int)kt.addr_vec.size(); i++) {
                    global_bank_id += kt.addr_vec[i] * multiplier;
                    if (i < bank_level) {
                      multiplier *= dram->m_organization.count[i];
                    }
                  }
                  
                  // Construct address signature from row/col for uniqueness
                  uint64_t addr_sig = 0;
                  int row_level = dram->m_levels("row");
                  int col_level = dram->m_levels("column");
                  if (row_level >= 0 && row_level < (int)kt.addr_vec.size()) {
                    addr_sig = kt.addr_vec[row_level];
                  }
                  if (col_level >= 0 && col_level < (int)kt.addr_vec.size()) {
                    addr_sig = (addr_sig << 16) | (kt.addr_vec[col_level] & 0xFFFF);
                  }
                  
                  // Update static weight mapping
                  if (global_bank_id >= 0 && global_bank_id < m_num_banks) {
                    m_kv_cache_policy->update_static_weight_mapping(global_bank_id, addr_sig);
                  }
                }
              }
            }
            break;  // Only process first kernel
          }
        }
      }
      
      // Generate interleaved trace: For each token, generate KV cache ops + kernel ops
      // This creates temporal overlap between KV cache and static weight operations
      if (m_enable_kv_cache && m_kv_trace_generator && kernel_idx >= 0) {
        for (int token_id = 0; token_id < m_num_tokens; token_id++) {
          // 1. Read KV cache for all previous tokens
          auto kv_traces = m_kv_trace_generator->generate_inference_step(token_id);
          for (const auto& [op, gen_addr_vec] : kv_traces) {
            Ramulator::AddrVec_t addr_vec(gen_addr_vec.begin(), gen_addr_vec.end());
            ::PIM::Trace new_trace;
            new_trace.op = op;
            new_trace.addr_vec = addr_vec;
            m_trace.push_back(new_trace);
            total_traces++;
          }
          
          // 2. Add kernel operations (attention computation with static weights)
          // This creates temporal overlap - kernel ops happen while KV cache is being accessed
          for (auto kt : pre_expanded_kernel_trace) {
            m_trace.push_back(kt);
            total_traces++;
            if (m_trace.size() > 10000000) { 
              break;
            }
          }
        }
        m_logger->info("Generated {} KV cache operations for {} tokens (interleaved with kernels)", 
                       total_traces, m_num_tokens);
      } else {
        // Fallback: Original behavior if KV cache is disabled
        for (auto t : old_trace) {
          if (t.op != "kernel") {
            m_trace.push_back(t);
            total_traces++;
          } else {
            std::vector<::PIM::Trace> kernel_trace;
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
        if (line.empty()) continue;

        std::string op = ""; 
        if (tokens[0] == "R") op = "read";
        else if (tokens[0] == "W") op = "write";
        else if (tokens[0] == "C") op = "compute";
        else if (tokens[0] == "SR") op = "subarray-read";
        else if (tokens[0] == "SW") op = "subarray-write";
        else if (tokens[0] == "BR") op = "bank-read";
        else if (tokens[0] == "BW") op = "bank-write";
        else if (tokens[0] == "conv2d" || tokens[0] == "gemm") {
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

          Ramulator::AddrVec_t addr_vec;
          for (const auto& token : addr_vec_tokens) {
            addr_vec.push_back(std::stoi(token)); 
          }

          ::PIM::Trace new_trace;
          new_trace.op = op;
          new_trace.addr_vec = addr_vec;
          m_trace.push_back(new_trace);

        } else if (op == "kernel_end") {
          op = "kernel";
          Ramulator::AddrVec_t addr_vec;
          addr_vec.push_back(m_kernels.size() - 1);
          
          ::PIM::Trace new_trace;
          new_trace.op = op;
          new_trace.addr_vec = addr_vec;
          m_trace.push_back(new_trace);
        }
      }

      trace_file.close();
    };

    bool is_finished() override {
      return m_memory_system->finished();
    };
    
    void finalize() override {
      if (m_enable_kv_cache && m_kv_cache_policy && m_conflict_tracker) {
        // Logging stats
      }
    }
};

}  // namespace Ramulator