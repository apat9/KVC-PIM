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
 */
class IKVCachePolicy {
  RAMULATOR_REGISTER_INTERFACE(IKVCachePolicy, "KVCachePolicy", "KV Cache Placement Policy Interface");

public:
  // REQUIRED: Base init function for Ramulator
  virtual void init() = 0;

  virtual void init(IDRAM* dram, int num_banks, 
                    const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) = 0;

  virtual int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) = 0;
  virtual int get_kv_cache_bank(int token_id) = 0;
  virtual bool has_bank_conflict(int bank_id) = 0;
  virtual void update_static_weight_mapping(int bank_id, uint64_t addr) = 0;
  virtual std::map<std::string, int64_t> get_stats() = 0;
  virtual void reset_stats() = 0;

  // REMOVED: virtual ~IKVCachePolicy() = default; (Macro handles this)
};

/**
 * @brief Naive KV cache placement policy (baseline)
 */
class NaiveKVCachePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, NaiveKVCachePolicy, "Naive", 
                                    "Round-robin KV cache placement (baseline)");

private:
  IDRAM* m_dram;
  int m_num_banks;
  int m_next_bank;
  std::map<int, int> m_token_to_bank;  
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  
  int64_t m_total_allocations;
  int64_t m_total_conflicts;

public:
  // REQUIRED: Empty init for Ramulator factory
  void init() override {}

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
    auto it = m_static_weight_mapping.find(bank_id);
    return (it != m_static_weight_mapping.end() && !it->second.empty());
  }

  void update_static_weight_mapping(int bank_id, uint64_t addr) override {
    // Naive policy doesn't need to track static weights dynamically
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
  
  int64_t m_total_allocations;
  int64_t m_total_conflicts;

public:
  // REQUIRED: Empty init
  void init() override {}

  void init(IDRAM* dram, int num_banks,
            const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_static_weight_mapping = static_weight_mapping;
    
    m_kv_cache_banks_count = param<int>("kv_cache_banks_count").default_val(num_banks / 4);
    m_kv_cache_banks_start = param<int>("kv_cache_banks_start").default_val(0);
    
    if (m_kv_cache_banks_start + m_kv_cache_banks_count > num_banks) {
      m_kv_cache_banks_count = num_banks - m_kv_cache_banks_start;
    }
    
    m_next_kv_bank = m_kv_cache_banks_start;
    m_total_allocations = 0;
    m_total_conflicts = 0;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    int bank_id = m_next_kv_bank;
    m_next_kv_bank = m_kv_cache_banks_start + 
                     ((m_next_kv_bank - m_kv_cache_banks_start + 1) % m_kv_cache_banks_count);
    
    m_token_to_bank[token_id] = bank_id;
    m_total_allocations++;
    
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
    if (bank_id >= m_kv_cache_banks_start && 
        bank_id < m_kv_cache_banks_start + m_kv_cache_banks_count) {
      auto it = m_static_weight_mapping.find(bank_id);
      return (it != m_static_weight_mapping.end() && !it->second.empty());
    }
    return false;
  }

  void update_static_weight_mapping(int bank_id, uint64_t addr) override {
    // Bank partitioning doesn't need to track static weights dynamically
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
 */
class ContentionAwarePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, ContentionAwarePolicy, "ContentionAware",
                                    "Smart KV cache placement based on static weight mapping");

private:
  IDRAM* m_dram;
  int m_num_banks;
  std::map<int, int> m_token_to_bank;
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  std::vector<int> m_bank_weight_count;
  std::vector<int> m_bank_kv_cache_count;  // Track KV cache allocations per bank
  int m_next_tiebreak_bank;  // For tie-breaking when all banks have equal scores
  
  int64_t m_total_allocations;
  int64_t m_total_conflicts;
  int64_t m_avg_weight_density;

public:
  // REQUIRED: Empty init
  void init() override {}

  void init(IDRAM* dram, int num_banks,
            const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_static_weight_mapping = static_weight_mapping;
    
    m_bank_weight_count.resize(num_banks, 0);
    m_bank_kv_cache_count.resize(num_banks, 0);
    for (const auto& [bank_id, weight_addrs] : static_weight_mapping) {
      if (bank_id >= 0 && bank_id < num_banks) {
        m_bank_weight_count[bank_id] = weight_addrs.size();
      }
    }
    
    m_next_tiebreak_bank = 0;
    m_total_allocations = 0;
    m_total_conflicts = 0;
    
    int total_weights = 0;
    for (int count : m_bank_weight_count) {
      total_weights += count;
    }
    m_avg_weight_density = (num_banks > 0) ? (total_weights / num_banks) : 0;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    // Score banks based on: static weight count + existing KV cache allocations
    // Lower score = better (less contention)
    int best_bank = -1;
    int best_score = std::numeric_limits<int>::max();
    std::vector<int> candidate_banks;  // Banks with the best score
    
    for (int bank_id = 0; bank_id < m_num_banks; bank_id++) {
      // Score = static weight count + existing KV cache count
      // Weight static weights more heavily (multiply by 2) since they're persistent
      int score = m_bank_weight_count[bank_id] * 2 + m_bank_kv_cache_count[bank_id];
      
      if (score < best_score) {
        best_score = score;
        candidate_banks.clear();
        candidate_banks.push_back(bank_id);
      } else if (score == best_score) {
        candidate_banks.push_back(bank_id);
      }
    }
    
    // Tie-breaking: if multiple banks have the same score, use round-robin
    if (candidate_banks.empty()) {
      best_bank = 0;  // Fallback
    } else if (candidate_banks.size() == 1) {
      best_bank = candidate_banks[0];
    } else {
      // Round-robin tie-breaking among candidates
      int candidate_idx = m_next_tiebreak_bank % candidate_banks.size();
      best_bank = candidate_banks[candidate_idx];
      m_next_tiebreak_bank = (m_next_tiebreak_bank + 1) % m_num_banks;
    }
    
    // Record the allocation
    m_token_to_bank[token_id] = best_bank;
    m_bank_kv_cache_count[best_bank]++;
    m_total_allocations++;
    
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
    if (bank_id < 0 || bank_id >= m_num_banks) return false;
    return m_bank_weight_count[bank_id] > 0;
  }

  void update_static_weight_mapping(int bank_id, uint64_t addr) override {
    if (bank_id >= 0 && bank_id < m_num_banks) {
      // Update weight count if this is a new address
      auto& weight_addrs = m_static_weight_mapping[bank_id];
      if (weight_addrs.find(addr) == weight_addrs.end()) {
        weight_addrs.insert(addr);
        m_bank_weight_count[bank_id]++;
      }
    }
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