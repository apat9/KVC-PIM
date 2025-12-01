#ifndef KV_CACHE_POLICY_H
#define KV_CACHE_POLICY_H

#include <vector>
#include <map>
#include <unordered_set>
#include <string>
#include <limits>
#include "base/base.h"
#include "dram/dram.h"

namespace Ramulator {
namespace PIM {

/**
 * @brief Interface for KV cache placement policies
 * 
 * This interface defines how KV cache blocks are placed in PIM banks,
 * with awareness of static weight mappings from OptiPIM.
 */
class IKVCachePolicy {
  RAMULATOR_REGISTER_INTERFACE(IKVCachePolicy, "KVCachePolicy", "KV Cache Placement Policy Interface");

public:
  /**
   * @brief Initialize the policy with architecture information
   * 
   * @param dram DRAM interface for architecture parameters
   * @param num_banks Total number of banks available
   * @param static_weight_mapping Map from bank_id to set of weight addresses in that bank
   */
  virtual void init(IDRAM* dram, int num_banks, 
                    const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) = 0;

  /**
   * @brief Allocate a bank for a new KV cache block
   * 
   * @param kv_cache_size Size of the KV cache block in bytes
   * @param token_id Token ID for this KV cache entry
   * @return Bank ID where the KV cache should be placed, or -1 if allocation failed
   */
  virtual int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) = 0;

  /**
   * @brief Get the bank ID for an existing KV cache entry
   * 
   * @param token_id Token ID
   * @return Bank ID, or -1 if not found
   */
  virtual int get_kv_cache_bank(int token_id) = 0;

  /**
   * @brief Check if a bank has conflicts with static weights
   * 
   * @param bank_id Bank ID to check
   * @return true if there are potential conflicts
   */
  virtual bool has_bank_conflict(int bank_id) = 0;

  /**
   * @brief Get statistics about the policy
   * 
   * @return Map of statistic names to values
   */
  virtual std::map<std::string, int64_t> get_stats() = 0;

  /**
   * @brief Reset statistics
   */
  virtual void reset_stats() = 0;

  virtual ~IKVCachePolicy() = default;
};

/**
 * @brief Naive KV cache placement policy (baseline)
 * 
 * Uses round-robin allocation without considering static weight mappings.
 * This serves as the baseline to demonstrate bank conflicts.
 */
class NaiveKVCachePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, NaiveKVCachePolicy, "Naive", 
                                    "Round-robin KV cache placement (baseline)");

private:
  IDRAM* m_dram;
  int m_num_banks;
  int m_next_bank;
  std::map<int, int> m_token_to_bank;  // token_id -> bank_id
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  
  // Statistics
  int64_t m_total_allocations;
  int64_t m_total_conflicts;

public:
  void init(IDRAM* dram, int num_banks,
            const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_next_bank = 0;
    m_static_weight_mapping = static_weight_mapping;
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    int bank_id = m_next_bank;
    m_next_bank = (m_next_bank + 1) % m_num_banks;
    
    m_token_to_bank[token_id] = bank_id;
    m_total_allocations++;
    
    // Check for conflicts
    if (has_bank_conflict(bank_id)) {
      m_total_conflicts++;
    }
    
    return bank_id;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    if (it != m_token_to_bank.end()) {
      return it->second;
    }
    return -1;
  }

  bool has_bank_conflict(int bank_id) override {
    // Check if this bank has static weights
    auto it = m_static_weight_mapping.find(bank_id);
    return (it != m_static_weight_mapping.end() && !it->second.empty());
  }

  std::map<std::string, int64_t> get_stats() override {
    return {
      {"total_allocations", m_total_allocations},
      {"total_conflicts", m_total_conflicts}
    };
  }

  void reset_stats() override {
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }
};

/**
 * @brief Bank Partitioning Policy
 * 
 * Reserves a set of banks exclusively for KV cache, leaving the rest for static weights.
 * OptiPIM will be run on the remaining banks.
 */
class BankPartitioningPolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, BankPartitioningPolicy, "BankPartitioning",
                                    "Reserve banks for KV cache");

private:
  IDRAM* m_dram;
  int m_num_banks;
  int m_kv_cache_banks_start;
  int m_kv_cache_banks_count;
  int m_next_kv_bank;
  std::map<int, int> m_token_to_bank;
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  
  // Statistics
  int64_t m_total_allocations;
  int64_t m_total_conflicts;

public:
  void init(IDRAM* dram, int num_banks,
            const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_static_weight_mapping = static_weight_mapping;
    
    // Reserve 25% of banks for KV cache (configurable)
    m_kv_cache_banks_count = param<int>("kv_cache_banks_count").default_val(num_banks / 4);
    m_kv_cache_banks_start = param<int>("kv_cache_banks_start").default_val(0);
    
    // Ensure we don't exceed available banks
    if (m_kv_cache_banks_start + m_kv_cache_banks_count > num_banks) {
      m_kv_cache_banks_count = num_banks - m_kv_cache_banks_start;
    }
    
    m_next_kv_bank = m_kv_cache_banks_start;
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    // Round-robin within the reserved KV cache banks
    int bank_id = m_next_kv_bank;
    m_next_kv_bank = m_kv_cache_banks_start + 
                     ((m_next_kv_bank - m_kv_cache_banks_start + 1) % m_kv_cache_banks_count);
    
    m_token_to_bank[token_id] = bank_id;
    m_total_allocations++;
    
    // Should have zero conflicts by design
    if (has_bank_conflict(bank_id)) {
      m_total_conflicts++;
    }
    
    return bank_id;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    if (it != m_token_to_bank.end()) {
      return it->second;
    }
    return -1;
  }

  bool has_bank_conflict(int bank_id) override {
    // Check if bank is in reserved range
    if (bank_id >= m_kv_cache_banks_start && 
        bank_id < m_kv_cache_banks_start + m_kv_cache_banks_count) {
      // Should not have static weights in reserved banks
      auto it = m_static_weight_mapping.find(bank_id);
      return (it != m_static_weight_mapping.end() && !it->second.empty());
    }
    return false;
  }

  std::map<std::string, int64_t> get_stats() override {
    return {
      {"total_allocations", m_total_allocations},
      {"total_conflicts", m_total_conflicts},
      {"reserved_banks", m_kv_cache_banks_count}
    };
  }

  void reset_stats() override {
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }
};

/**
 * @brief Contention-Aware Policy
 * 
 * Intelligently places KV cache blocks in banks with least static weight usage.
 * Analyzes the static weight mapping to find banks with minimal conflicts.
 */
class ContentionAwarePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, ContentionAwarePolicy, "ContentionAware",
                                    "Smart KV cache placement based on static weight mapping");

private:
  IDRAM* m_dram;
  int m_num_banks;
  std::map<int, int> m_token_to_bank;
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  std::vector<int> m_bank_weight_count;  // Number of weight addresses per bank
  
  // Statistics
  int64_t m_total_allocations;
  int64_t m_total_conflicts;
  int64_t m_avg_weight_density;

public:
  void init(IDRAM* dram, int num_banks,
            const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_static_weight_mapping = static_weight_mapping;
    
    // Initialize bank weight counts
    m_bank_weight_count.resize(num_banks, 0);
    for (const auto& [bank_id, weight_addrs] : static_weight_mapping) {
      if (bank_id >= 0 && bank_id < num_banks) {
        m_bank_weight_count[bank_id] = weight_addrs.size();
      }
    }
    
    m_total_allocations = 0;
    m_total_conflicts = 0;
    
    // Calculate average weight density
    int total_weights = 0;
    for (int count : m_bank_weight_count) {
      total_weights += count;
    }
    m_avg_weight_density = (num_banks > 0) ? (total_weights / num_banks) : 0;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    // Find bank with minimum weight density (least conflicts)
    int best_bank = -1;
    int min_weight_count = std::numeric_limits<int>::max();
    
    for (int bank_id = 0; bank_id < m_num_banks; bank_id++) {
      if (m_bank_weight_count[bank_id] < min_weight_count) {
        min_weight_count = m_bank_weight_count[bank_id];
        best_bank = bank_id;
      }
    }
    
    if (best_bank == -1) {
      best_bank = 0;  // Fallback
    }
    
    m_token_to_bank[token_id] = best_bank;
    m_total_allocations++;
    
    // Check for conflicts
    if (has_bank_conflict(best_bank)) {
      m_total_conflicts++;
    }
    
    return best_bank;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    if (it != m_token_to_bank.end()) {
      return it->second;
    }
    return -1;
  }

  bool has_bank_conflict(int bank_id) override {
    if (bank_id < 0 || bank_id >= m_num_banks) {
      return false;
    }
    return m_bank_weight_count[bank_id] > 0;
  }

  std::map<std::string, int64_t> get_stats() override {
    return {
      {"total_allocations", m_total_allocations},
      {"total_conflicts", m_total_conflicts},
      {"avg_weight_density", m_avg_weight_density}
    };
  }

  void reset_stats() override {
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }
};

}  // namespace PIM
}  // namespace Ramulator

#endif  // KV_CACHE_POLICY_H

