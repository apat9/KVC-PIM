#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>  // Added for min_element
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

  ::PIM::IPimCodeGen* m_pim_codegen = nullptr;
  PIM::IKVCachePolicy* m_kv_cache_policy = nullptr;
  BankConflictTracker* m_conflict_tracker = nullptr;
  PIM::KVCacheTraceGenerator* m_kv_trace_generator = nullptr;

  // Configuration
  std::string m_static_weight_trace_path;
  bool m_enable_kv_cache = false;
  int m_num_tokens = 512;
  int m_num_banks = 0;
  int m_kernel_slice_ops_per_token = 5000;

  // Debug counters
  int m_total_weight_ops = 0;
  int m_unique_weight_banks = 0;
  std::map<int, int> m_bank_weight_counts;

public:
  void init() override {
    std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
    m_clock_ratio = param<uint>("clock_ratio").required();
    m_enable_kv_cache = param<bool>("enable_kv_cache").default_val(false);
    m_static_weight_trace_path = param<std::string>("static_weight_trace_path").default_val("");
    m_num_tokens = param<int>("num_tokens").default_val(512);
    m_kernel_slice_ops_per_token = param<int>("kernel_slice_ops_per_token")
        .desc("Number of kernel instructions executed per token (0 = pure KV-cache mode)")
        .default_val(5000);

    m_pim_codegen = create_child_ifce<::PIM::IPimCodeGen>();
    if (m_enable_kv_cache) {
      m_kv_cache_policy = create_child_ifce<PIM::IKVCachePolicy>();
    }

    m_logger = Logging::create_logger("PIM Trace KV-Aware");
    m_logger->info("Loading trace file {} ...", trace_path_str);
    init_trace(trace_path_str);
    m_logger->info("Loaded {} high-level trace lines. Starting expansion...", m_trace.size());
  }

  void connect_memory_system(IMemorySystem* memory_system) override {
    IFrontEnd::connect_memory_system(memory_system);

    if (m_enable_kv_cache && m_kv_cache_policy) {
      IDRAM* dram = memory_system->get_ifce<IDRAM>();
      m_num_banks = dram->get_level_size("bank") *
                    dram->get_level_size("bankgroup") *
                    dram->get_level_size("channel");

      m_logger->info("DRAM has {} total banks", m_num_banks);

      // Load static weights from file if provided
      std::map<int, std::unordered_set<uint64_t>> static_weight_map;
      
      if (!m_static_weight_trace_path.empty()) {
        static_weight_map = PIM::StaticWeightLoader::extract_weight_banks(
            m_static_weight_trace_path, m_num_banks);
        m_logger->info("Loaded static weights from file: {} banks with weights", 
                       static_weight_map.size());
      }

      m_kv_cache_policy->init(dram, m_num_banks, static_weight_map);

      m_conflict_tracker = new BankConflictTracker(m_num_banks);
      m_kv_trace_generator = new PIM::KVCacheTraceGenerator(
          m_kv_cache_policy, m_conflict_tracker, dram, m_num_banks);

      expand_trace();
    } else {
      expand_trace();
    }
  }

  void tick() override {
    if (m_curr_trace_idx >= m_trace_length) return;

    const ::PIM::Trace& t = m_trace[m_curr_trace_idx];

    Ramulator::AddrVec_t req_addr_vec;
    req_addr_vec.reserve(t.addr_vec.size());
    for (auto val : t.addr_vec) {
      req_addr_vec.push_back(static_cast<int>(val));
    }

    bool sent = m_memory_system->send({req_addr_vec, t.op});
    if (sent) {
      m_curr_trace_idx++;
      if (m_curr_trace_idx % 1000000 == 0) {
        m_logger->info("Progress: {} / {} traces", m_curr_trace_idx, m_trace_length);
      }
    }
  }

private:
  void expand_trace() {
    m_logger->info("PHASE 1: Expanding Kernels (Pre-Scan)...");
    
    std::vector<::PIM::Trace> expanded_kernel_ops;
    expanded_kernel_ops.reserve(5000000); 

    std::vector<::PIM::Trace> old_trace = std::move(m_trace);
    m_trace.clear();
    m_trace.reserve(old_trace.size() + (m_num_tokens * 1000)); 

    int kernel_count = 0;
    size_t total_instructions = 0;
    const size_t SAFETY_LIMIT = 5000000;

    // PHASE 1: Expand kernels (with logging)
    for (const auto& t : old_trace) {
      if (t.op == "kernel") {
        kernel_count++;
        int kernel_id = static_cast<int>(t.addr_vec[0]);
        
        m_logger->info("  -> Expanding Kernel #{} (ID: {})...", kernel_count, kernel_id);
        
        size_t size_before = expanded_kernel_ops.size();
        m_pim_codegen->codegen_kernel(m_kernels[kernel_id], expanded_kernel_ops);
        size_t generated = expanded_kernel_ops.size() - size_before;
        
        total_instructions += generated;
        m_logger->info("     Generated {} instructions. Total: {}", generated, total_instructions);

        if (total_instructions > SAFETY_LIMIT) {
            m_logger->warn("⚠️ SAFETY LIMIT REACHED ({} ops). Stopping pre-scan expansion.", total_instructions);
            break;
        }
      }
    }
    
    // Debug: Analyze the expanded trace
    debug_trace_analysis(expanded_kernel_ops);
    
    m_logger->info("PHASE 2: Building Weight Map from {} instructions...", expanded_kernel_ops.size());

    // PHASE 2: Build Map
    if (m_enable_kv_cache && m_kv_cache_policy) {
      std::map<int, std::unordered_set<uint64_t>> live_weight_map;
      m_total_weight_ops = 0;
      m_unique_weight_banks = 0;
      m_bank_weight_counts.clear();
      
      // Track operation distribution per bank
      std::map<int, std::set<std::string>> bank_operations;
      std::map<int, int> bank_op_counts;
      
      for (const auto& t : expanded_kernel_ops) {
        // Count operations per bank
        if (!t.addr_vec.empty() && t.addr_vec.size() >= 4) {
          int bank_id = static_cast<int>(t.addr_vec[3]);
          
          if (bank_id >= 0 && bank_id < m_num_banks) {
            bank_operations[bank_id].insert(t.op);
            bank_op_counts[bank_id]++;
            
            // If it's a write operation, mark as weight
            if (t.op == "write") {
              // Use bank ID + operation counter as unique signature
              uint64_t signature = (static_cast<uint64_t>(bank_id) << 32) | m_total_weight_ops;
              
              live_weight_map[bank_id].insert(signature);
              m_bank_weight_counts[bank_id]++;
              m_total_weight_ops++;
            }
          }
        }
      }
      
      m_unique_weight_banks = live_weight_map.size();
      
      // Log bank statistics
      m_logger->info("  -> Bank operation statistics:");
      for (const auto& [bank, count] : bank_op_counts) {
        std::string ops_str;
        auto it = bank_operations.find(bank);
        if (it != bank_operations.end()) {
          for (const auto& op : it->second) ops_str += op + " ";
        }
        m_logger->info("     Bank {}: {} total operations [{}]", bank, count, ops_str);
      }
      
      // CRITICAL: Check if we found any weights
      if (live_weight_map.empty()) {
        m_logger->warn("⚠️ No weight banks automatically detected!");
        m_logger->info("  -> Using heuristic: banks with write operations will be marked as weight banks");
        
        // Find banks that have write operations
        for (const auto& [bank, ops] : bank_operations) {
          if (ops.find("write") != ops.end()) {
            // Mark as weight bank with synthetic addresses
            for (int i = 0; i < 100; i++) {  // Add reasonable number of synthetic weights
              uint64_t signature = (static_cast<uint64_t>(bank) << 32) | i;
              live_weight_map[bank].insert(signature);
            }
            m_logger->info("  -> Bank {} marked as weight bank (has write operations)", bank);
          }
        }
        
        m_total_weight_ops = 0;
        for (const auto& [bank, addrs] : live_weight_map) {
          m_total_weight_ops += addrs.size();
          m_bank_weight_counts[bank] = addrs.size();
        }
        m_unique_weight_banks = live_weight_map.size();
        
        if (live_weight_map.empty()) {
          m_logger->warn("⚠️ Still no weight banks! All banks will be treated equally.");
        }
      }
      
      m_logger->info("  -> Final weight map: {} unique weight addresses in {} banks", 
                     m_total_weight_ops, m_unique_weight_banks);
      
      // Log the actual weight banks being used
      if (!live_weight_map.empty()) {
        m_logger->info("  -> Weight banks (avoid for KV cache):");
        for (const auto& [bank, addrs] : live_weight_map) {
          m_logger->info("     Bank {}: {} weight addresses", bank, addrs.size());
        }
      }
      
      m_kv_cache_policy->set_static_weight_mapping(live_weight_map);
      
      // IMPORTANT: Log what we're telling the policy
      m_logger->info("=== KV CACHE POLICY CONFIGURATION ===");
      m_logger->info("  Total banks: {}", m_num_banks);
      m_logger->info("  Weight banks: {}", m_unique_weight_banks);
      m_logger->info("  KV tokens to allocate: {}", m_num_tokens);
      m_logger->info("  Available banks for KV: {}", m_num_banks - m_unique_weight_banks);
    }

    // PHASE 3: Trace Generation
    m_logger->info("PHASE 3: Generating Trace for {} tokens...", m_num_tokens);
    
    int kv_ops = 0;
    if (m_enable_kv_cache && m_kv_trace_generator) {
      
      size_t kernel_slice_size = static_cast<size_t>(m_kernel_slice_ops_per_token);
      if (kernel_slice_size > expanded_kernel_ops.size()) kernel_slice_size = expanded_kernel_ops.size();
      if (kernel_slice_size == 0) m_logger->info(" -> Pure KV-cache mode (no kernel slice)");
      m_logger->info(" -> Kernel Slice Size: {} ops/token", kernel_slice_size);

      for (int token_id = 0; token_id < m_num_tokens; ++token_id) {
        
        if (token_id % 50 == 0) m_logger->info("     Generating Token {}/{}", token_id, m_num_tokens);

        // 1. KV Ops
        auto kv_traces = m_kv_trace_generator->generate_inference_step(token_id);
        for (const auto& [op, gen_addr_vec] : kv_traces) {
          Ramulator::AddrVec_t addr_vec;
          addr_vec.reserve(gen_addr_vec.size());
          for (uint64_t v : gen_addr_vec) addr_vec.push_back(static_cast<int>(v));
          m_trace.push_back({op, addr_vec});
          kv_ops++;
        }

        // 2. Kernel Slice
        if (!expanded_kernel_ops.empty()) {
           size_t start = (token_id * kernel_slice_size) % expanded_kernel_ops.size();
           for(size_t i = 0; i < kernel_slice_size; i++) {
               size_t idx = (start + i) % expanded_kernel_ops.size();
               m_trace.push_back(expanded_kernel_ops[idx]);
           }
        }
      }
      m_logger->info("  -> Generated {} KV cache operations.", kv_ops);
    } else {
      // Fallback
      for(const auto& t : expanded_kernel_ops) {
          Ramulator::AddrVec_t addr_vec;
          for(auto v : t.addr_vec) addr_vec.push_back(static_cast<int>(v));
          m_trace.push_back({t.op, addr_vec});
      }
    }

    m_trace_length = m_trace.size();
    m_logger->info("=== FINAL TRACE READY: {} total operations ===", m_trace_length);
    m_logger->info("=== Weight Stats: {} ops in {} banks ===", 
                   m_total_weight_ops, m_unique_weight_banks);
  }

  void debug_trace_analysis(const std::vector<::PIM::Trace>& traces) {
    std::map<std::string, int> op_counts;
    std::set<int> unique_bank_ids;
    std::map<int, int> bank_op_counts;
    std::map<int, std::set<std::string>> bank_op_types;
    
    for (const auto& t : traces) {
      op_counts[t.op]++;
      
      if (!t.addr_vec.empty() && t.addr_vec.size() >= 4) {
        int bank_id = static_cast<int>(t.addr_vec[3]);
        unique_bank_ids.insert(bank_id);
        bank_op_counts[bank_id]++;
        bank_op_types[bank_id].insert(t.op);
      }
    }
    
    m_logger->info("=== EXPANDED TRACE DEBUG ===");
    m_logger->info("Total operations: {}", traces.size());
    
    m_logger->info("Operation distribution:");
    for (const auto& [op, count] : op_counts) {
      m_logger->info("  {}: {}", op, count);
    }
    
    m_logger->info("Bank operation distribution:");
    for (int bank : unique_bank_ids) {
      auto types_it = bank_op_types.find(bank);
      std::string type_str;
      if (types_it != bank_op_types.end()) {
        for (const auto& op : types_it->second) type_str += op + " ";
      }
      m_logger->info("  Bank {}: {} operations [{}]", bank, bank_op_counts[bank], type_str);
    }
    
    // Sample some actual addresses
    m_logger->info("Sample addresses (first 10):");
    int samples_shown = 0;
    for (const auto& t : traces) {
      if (!t.addr_vec.empty() && t.addr_vec.size() >= 4) {
        m_logger->info("  Op: '{}', Bank: {}, Addr: [{}...]", 
                      t.op, t.addr_vec[3], 
                      t.addr_vec.size() > 0 ? std::to_string(t.addr_vec[0]) : "N/A");
        samples_shown++;
        if (samples_shown >= 10) break;
      }
    }
  }

  void init_trace(const std::string& file_path_str) {
    fs::path trace_path(file_path_str);
    if (!fs::exists(trace_path)) {
      throw ConfigurationError("Trace {} does not exist!", file_path_str);
    }

    std::ifstream file(trace_path);
    if (!file.is_open()) {
      throw ConfigurationError("Cannot open trace: {}", file_path_str);
    }

    std::string line;
    std::string kernel_cmd;
    std::vector<std::vector<std::string>> current_kernel;

    while (std::getline(file, line)) {
      if (line.empty()) continue;

      std::vector<std::string> tokens;
      tokenize(tokens, line, " ");

      if (tokens[0] == "R") m_trace.push_back({"read", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "W") m_trace.push_back({"write", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "C") m_trace.push_back({"compute", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "SR") m_trace.push_back({"subarray-read", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "SW") m_trace.push_back({"subarray-write", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "BR") m_trace.push_back({"bank-read", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "BW") m_trace.push_back({"bank-write", parse_addr_vec(tokens[1])});
      else if (tokens[0] == "conv2d" || tokens[0] == "gemm") {
        kernel_cmd = tokens[0];
        current_kernel.clear();
        current_kernel.push_back(tokens);
      }
      else if (tokens[0] == "end" && !kernel_cmd.empty()) {
        m_kernels.push_back(current_kernel);
        kernel_cmd.clear();
        m_trace.push_back({"kernel", {static_cast<int64_t>(m_kernels.size() - 1)}});
      }
      else if (!kernel_cmd.empty()) {
        current_kernel.push_back(tokens);
      }
    }
    file.close();
  }

  Ramulator::AddrVec_t parse_addr_vec(const std::string& s) {
    std::vector<std::string> parts;
    tokenize(parts, s, ",");
    Ramulator::AddrVec_t vec;
    for (const auto& p : parts) {
      vec.push_back(static_cast<int>(std::stoll(p)));
    }
    return vec;
  }

  bool is_finished() override {
    return m_curr_trace_idx >= m_trace_length && m_memory_system->finished();
  }

  void finalize() override {
    if (m_enable_kv_cache && m_kv_cache_policy) {
      auto stats = m_kv_cache_policy->get_stats();
      m_logger->info("=== FINAL KV CACHE POLICY STATS ===");
      for (const auto& [k, v] : stats) {
        m_logger->info("  {}: {}", k, v);
      }
      m_logger->info("=== WEIGHT DETECTION SUMMARY ===");
      m_logger->info("  Total weight ops detected: {}", m_total_weight_ops);
      m_logger->info("  Unique weight banks: {}", m_unique_weight_banks);
      
      // Calculate conflicts percentage
      auto total_allocs_it = stats.find("total_allocations");
      auto total_conflicts_it = stats.find("total_conflicts");
      if (total_allocs_it != stats.end() && total_conflicts_it != stats.end() && 
          total_allocs_it->second > 0) {
        double conflict_rate = (static_cast<double>(total_conflicts_it->second) / 
                               total_allocs_it->second) * 100.0;
        m_logger->info("  KV Cache conflict rate: {:.2f}%", conflict_rate);
      }
    }
  }
};

} // namespace Ramulator