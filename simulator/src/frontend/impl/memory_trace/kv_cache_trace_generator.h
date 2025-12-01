#ifndef KV_CACHE_TRACE_GENERATOR_H
#define KV_CACHE_TRACE_GENERATOR_H

#include <vector>
#include <string>
#include <map>
#include "pim_codegen/kv_cache_policy.h"
#include "pim_codegen/static_weight_loader.h"
#include "memory_system/bank_conflict_tracker.h"

namespace Ramulator {
namespace PIM {

/**
 * @brief Generates KV cache operations for LLM inference traces
 * 
 * This class generates memory operations for KV cache read/write operations
 * that occur during autoregressive LLM inference. It uses a KV cache placement
 * policy to determine which banks to use for each KV cache entry.
 */
class KVCacheTraceGenerator {
private:
  IKVCachePolicy* m_kv_cache_policy;
  BankConflictTracker* m_conflict_tracker;
  IDRAM* m_dram;
  int m_num_banks;
  int m_current_token_id;
  
  // KV cache parameters
  int m_kv_cache_head_dim;
  int m_kv_cache_hidden_dim;
  int m_kv_cache_block_size;  // Size of each KV cache block in bytes
  
  // Track KV cache allocations
  std::map<int, int> m_token_to_bank;  // token_id -> bank_id

public:
  KVCacheTraceGenerator(IKVCachePolicy* policy, BankConflictTracker* tracker,
                        IDRAM* dram, int num_banks)
    : m_kv_cache_policy(policy), m_conflict_tracker(tracker),
      m_dram(dram), m_num_banks(num_banks), m_current_token_id(0),
      m_kv_cache_head_dim(128), m_kv_cache_hidden_dim(4096),
      m_kv_cache_block_size(4096) {
  }

  /**
   * @brief Generate KV cache write operation for a new token
   * 
   * @param token_id Token ID (0 for prompt, 1+ for generated tokens)
   * @param kv_data_size Size of K and V cache data for this token
   * @return Vector of trace operations (address vectors)
   */
  std::vector<std::pair<std::string, std::vector<uint64_t>>> generate_kv_cache_write(
      int token_id, size_t kv_data_size) {
    
    std::vector<std::pair<std::string, std::vector<uint64_t>>> traces;
    
    // Allocate bank for this KV cache entry
    int bank_id = m_kv_cache_policy->allocate_kv_cache_bank(kv_data_size, token_id);
    if (bank_id < 0) {
      return traces;  // Allocation failed
    }
    
    m_token_to_bank[token_id] = bank_id;
    
    // Generate write operations for K and V cache
    // For simplicity, we write to consecutive rows in the allocated bank
    int num_rows = (kv_data_size + 8192 - 1) / 8192;  // Assuming 8KB per row
    
    for (int row = 0; row < num_rows; row++) {
      std::vector<uint64_t> addr_vec;
      addr_vec.push_back(0);  // channel (assume single channel for now)
      addr_vec.push_back(bank_id);
      addr_vec.push_back(row);  // row
      addr_vec.push_back(0);    // column
      
      traces.push_back({"write", addr_vec});
    }
    
    return traces;
  }

  /**
   * @brief Generate KV cache read operations for attention computation
   * 
   * @param token_ids Vector of token IDs to read (all previous tokens + current)
   * @return Vector of trace operations
   */
  std::vector<std::pair<std::string, std::vector<uint64_t>>> generate_kv_cache_read(
      const std::vector<int>& token_ids) {
    
    std::vector<std::pair<std::string, std::vector<uint64_t>>> traces;
    
    for (int token_id : token_ids) {
      int bank_id = m_kv_cache_policy->get_kv_cache_bank(token_id);
      if (bank_id < 0) {
        continue;  // Token not found
      }
      
      // Generate read operations for this token's KV cache
      int num_rows = (m_kv_cache_block_size + 8192 - 1) / 8192;
      
      for (int row = 0; row < num_rows; row++) {
        std::vector<uint64_t> addr_vec;
        addr_vec.push_back(0);  // channel
        addr_vec.push_back(bank_id);
        addr_vec.push_back(row);  // row
        addr_vec.push_back(0);    // column
        
        traces.push_back({"read", addr_vec});
      }
    }
    
    return traces;
  }

  /**
   * @brief Generate a complete inference step trace
   * 
   * Generates KV cache operations for one step of autoregressive generation:
   * 1. Read KV cache for all previous tokens
   * 2. Compute attention
   * 3. Write KV cache for new token
   * 
   * @param current_token_id Current token being generated
   * @return Vector of trace operations
   */
  std::vector<std::pair<std::string, std::vector<uint64_t>>> generate_inference_step(
      int current_token_id) {
    
    std::vector<std::pair<std::string, std::vector<uint64_t>>> traces;
    
    // Read KV cache for all previous tokens (0 to current_token_id-1)
    std::vector<int> previous_tokens;
    for (int i = 0; i < current_token_id; i++) {
      previous_tokens.push_back(i);
    }
    
    auto read_traces = generate_kv_cache_read(previous_tokens);
    traces.insert(traces.end(), read_traces.begin(), read_traces.end());
    
    // Write KV cache for new token
    size_t kv_data_size = m_kv_cache_head_dim * m_kv_cache_hidden_dim * sizeof(float) * 2;  // K and V
    auto write_traces = generate_kv_cache_write(current_token_id, kv_data_size);
    traces.insert(traces.end(), write_traces.begin(), write_traces.end());
    
    return traces;
  }

  /**
   * @brief Set KV cache parameters
   */
  void set_kv_cache_params(int head_dim, int hidden_dim, int block_size) {
    m_kv_cache_head_dim = head_dim;
    m_kv_cache_hidden_dim = hidden_dim;
    m_kv_cache_block_size = block_size;
  }
};

}  // namespace PIM
}  // namespace Ramulator

#endif  // KV_CACHE_TRACE_GENERATOR_H

