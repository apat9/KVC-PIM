#ifndef KV_CACHE_POLICY_H
#define KV_CACHE_POLICY_H

#include <vector>
#include <map>
#include <unordered_set>
#include <string>
#include <limits>
#include <algorithm>
#include <numeric>
#include "base/base.h"
#include "dram/dram.h"

namespace Ramulator {
namespace PIM {

class IKVCachePolicy {
  RAMULATOR_REGISTER_INTERFACE(IKVCachePolicy, "KVCachePolicy", "KV Cache Placement Policy Interface");

public:
  virtual void init() = 0;
  virtual void init(IDRAM* dram, int num_banks, 
                    const std::map<int, std::unordered_set<uint64_t>>& static_weight_mapping) = 0;

  // [NEW] Dedicated function to update map without re-registering params
  virtual void set_static_weight_mapping(const std::map<int, std::unordered_set<uint64_t>>& mapping) = 0;

  virtual int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) = 0;
  virtual int get_kv_cache_bank(int token_id) = 0;
  virtual bool has_bank_conflict(int bank_id) = 0;
  virtual std::map<std::string, int64_t> get_stats() = 0;
  virtual void reset_stats() = 0;
};

class NaiveKVCachePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, NaiveKVCachePolicy, "Naive", 
                                    "Round-robin KV cache placement (baseline)");
private:
  IDRAM* m_dram;
  int m_num_banks;
  int m_next_bank;
  std::map<int, int> m_token_to_bank;  
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  int64_t m_total_allocations = 0;
  int64_t m_total_conflicts = 0;

public:
  void init() override {}
  void init(IDRAM* dram, int num_banks, const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_next_bank = 0;
    m_static_weight_mapping = mapping;
  }

  // Naive policy ignores weights, so this is just a stored update
  void set_static_weight_mapping(const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
      m_static_weight_mapping = mapping;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    if (m_num_banks <= 0) return 0;
    int bank_id = m_next_bank;
    m_next_bank = (m_next_bank + 1) % m_num_banks;
    m_token_to_bank[token_id] = bank_id;
    m_total_allocations++;
    if (has_bank_conflict(bank_id)) m_total_conflicts++;
    return bank_id;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    return (it != m_token_to_bank.end()) ? it->second : -1;
  }

  bool has_bank_conflict(int bank_id) override {
    auto it = m_static_weight_mapping.find(bank_id);
    return (it != m_static_weight_mapping.end() && !it->second.empty());
  }

  std::map<std::string, int64_t> get_stats() override {
    return {{"total_allocations", m_total_allocations}, {"total_conflicts", m_total_conflicts}};
  }
  void reset_stats() override { m_total_allocations = 0; m_total_conflicts = 0; }
};

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
  int64_t m_total_allocations = 0;
  int64_t m_total_conflicts = 0;

public:
  void init() override {}
  void init(IDRAM* dram, int num_banks, const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    m_static_weight_mapping = mapping;
    
    // PARAM REGISTRATION HAPPENS HERE (Only call once!)
    m_kv_cache_banks_count = param<int>("kv_cache_banks_count").default_val(4);
    m_kv_cache_banks_start = param<int>("kv_cache_banks_start").default_val(0);
    
    if (m_kv_cache_banks_count <= 0) m_kv_cache_banks_count = 1;
    if (m_kv_cache_banks_start < 0) m_kv_cache_banks_start = 0;
    if (m_kv_cache_banks_start + m_kv_cache_banks_count > num_banks) {
       if (m_kv_cache_banks_start < num_banks) m_kv_cache_banks_count = num_banks - m_kv_cache_banks_start;
       else { m_kv_cache_banks_start = 0; m_kv_cache_banks_count = (num_banks > 4) ? 4 : 1; }
    }
    m_next_kv_bank = m_kv_cache_banks_start;
  }

  // Safe update function
  void set_static_weight_mapping(const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
      m_static_weight_mapping = mapping;
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    if (m_kv_cache_banks_count <= 0) return 0;
    int bank_id = m_next_kv_bank;
    int offset = m_next_kv_bank - m_kv_cache_banks_start;
    offset = (offset + 1) % m_kv_cache_banks_count;
    m_next_kv_bank = m_kv_cache_banks_start + offset;
    
    m_token_to_bank[token_id] = bank_id;
    m_total_allocations++;
    if (has_bank_conflict(bank_id)) m_total_conflicts++;
    return bank_id;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    return (it != m_token_to_bank.end()) ? it->second : -1;
  }

  bool has_bank_conflict(int bank_id) override {
    if (bank_id >= m_kv_cache_banks_start && bank_id < m_kv_cache_banks_start + m_kv_cache_banks_count) {
      auto it = m_static_weight_mapping.find(bank_id);
      return (it != m_static_weight_mapping.end() && !it->second.empty());
    }
    return false;
  }

  std::map<std::string, int64_t> get_stats() override {
    return {{"total_allocations", m_total_allocations}, {"total_conflicts", m_total_conflicts}, {"reserved_banks", m_kv_cache_banks_count}};
  }
  void reset_stats() override { m_total_allocations = 0; m_total_conflicts = 0; }
};

class ContentionAwarePolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, ContentionAwarePolicy, "ContentionAware",
                                    "Smart KV cache placement based on static weight mapping");
private:
  IDRAM* m_dram;
  int m_num_banks;
  std::map<int, int> m_token_to_bank;
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  std::vector<int> m_static_weight_count; 
  std::vector<int> m_dynamic_alloc_count;
  int64_t m_total_allocations = 0;
  int64_t m_total_conflicts = 0;
  
  // Debug tracking
  std::vector<int> m_allocation_order;
  Logger_t m_logger;

public:
  void init() override {}
  
  void init(IDRAM* dram, int num_banks, const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    if (num_banks <= 0) m_num_banks = 1;
    
    m_dynamic_alloc_count.assign(m_num_banks, 0);
    m_logger = Logging::create_logger("ContentionAwarePolicy");
    
    // Use the setter logic to initialize weights
    set_static_weight_mapping(mapping);
    
    m_logger->info("Initialized with {} banks", m_num_banks);
  }

  void set_static_weight_mapping(const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
      m_static_weight_mapping = mapping;
      m_static_weight_count.assign(m_num_banks, 0);
      
      int total_weight_banks = 0;
      for (const auto& [bank_id, weight_addrs] : m_static_weight_mapping) {
        if (bank_id >= 0 && bank_id < m_num_banks) {
          m_static_weight_count[bank_id] = weight_addrs.size();
          total_weight_banks++;
        }
      }
      
      m_logger->info("Weight mapping updated: {} banks have static weights", total_weight_banks);
      for (int i = 0; i < m_num_banks; i++) {
        if (m_static_weight_count[i] > 0) {
          m_logger->info("  Bank {}: {} weight addresses", i, m_static_weight_count[i]);
        }
      }
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    // SIMPLE BUT EFFECTIVE: Spread KV cache, avoid weight banks
    static int last_allocated_bank = 3; // Start after likely weight banks
    
    // Find a good bank
    int best_bank = -1;
    int attempts = 0;
    
    while (attempts < m_num_banks * 2) {
      last_allocated_bank = (last_allocated_bank + 1) % m_num_banks;
      attempts++;
      
      // Prefer banks without weights
      if (m_static_weight_count[last_allocated_bank] == 0) {
        // Also check if this bank doesn't have too many allocations already
        if (m_dynamic_alloc_count[last_allocated_bank] < 3) { // Max 3 KV caches per bank
          best_bank = last_allocated_bank;
          break;
        }
      }
    }
    
    // If we didn't find a good bank, pick the one with fewest allocations
    if (best_bank == -1) {
      int min_allocations = std::numeric_limits<int>::max();
      for (int bank_id = 0; bank_id < m_num_banks; bank_id++) {
        if (m_dynamic_alloc_count[bank_id] < min_allocations) {
          min_allocations = m_dynamic_alloc_count[bank_id];
          best_bank = bank_id;
        }
      }
    }
    
    if (best_bank == -1) best_bank = 0;
    
    // Record allocation
    m_token_to_bank[token_id] = best_bank;
    m_dynamic_alloc_count[best_bank]++;
    m_total_allocations++;
    m_allocation_order.push_back(best_bank);
    
    // Check for conflict
    bool has_conflict = has_bank_conflict(best_bank);
    if (has_conflict) {
      m_total_conflicts++;
    }
    
    // Debug logging for first 20 allocations
    if (m_total_allocations <= 20) {
      m_logger->info("Allocation #{}: Token {} -> Bank {} (weights: {}, existing KV: {}, conflict: {})",
                    m_total_allocations, token_id, best_bank,
                    m_static_weight_count[best_bank], m_dynamic_alloc_count[best_bank],
                    has_conflict ? "YES" : "NO");
    }
    
    return best_bank;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    return (it != m_token_to_bank.end()) ? it->second : -1;
  }

  bool has_bank_conflict(int bank_id) override {
    if (bank_id < 0 || bank_id >= m_num_banks) return false;
    return m_static_weight_count[bank_id] > 0;
  }

  std::map<std::string, int64_t> get_stats() override {
    // Calculate bank distribution
    std::map<int, int> bank_distribution;
    for (int bank : m_allocation_order) {
      bank_distribution[bank]++;
    }
    
    // Log distribution
    m_logger->info("=== Final KV Cache Bank Distribution ===");
    for (int bank = 0; bank < m_num_banks; bank++) {
      if (m_dynamic_alloc_count[bank] > 0 || m_static_weight_count[bank] > 0) {
        m_logger->info("  Bank {}: {} KV caches, {} weights", 
                      bank, m_dynamic_alloc_count[bank], m_static_weight_count[bank]);
      }
    }
    
    return {
      {"total_allocations", m_total_allocations}, 
      {"total_conflicts", m_total_conflicts},
      {"weight_banks", static_cast<int64_t>(std::count_if(
        m_static_weight_count.begin(), m_static_weight_count.end(),
        [](int count) { return count > 0; }))}
    };
  }
  
  void reset_stats() override {
    m_total_allocations = 0;
    m_total_conflicts = 0;
    std::fill(m_dynamic_alloc_count.begin(), m_dynamic_alloc_count.end(), 0);
    m_allocation_order.clear();
  }
};

class SmartLocalityPolicy final : public IKVCachePolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IKVCachePolicy, SmartLocalityPolicy, "SmartLocality",
                                    "Balance conflict avoidance with bank locality");
private:
  IDRAM* m_dram;
  int m_num_banks;
  std::map<int, int> m_token_to_bank;
  std::map<int, std::unordered_set<uint64_t>> m_static_weight_mapping;
  std::vector<int> m_static_weight_count; 
  std::vector<int> m_dynamic_alloc_count;
  std::vector<int> m_bank_activity;  // Estimated bank activity level (0-100)
  int64_t m_total_allocations = 0;
  int64_t m_total_conflicts = 0;
  
  // Parameters
  double m_locality_weight;
  int m_max_kv_per_bank;
  int m_activity_threshold_percent;
  
  Logger_t m_logger;

public:
  void init() override {}
  
  void init(IDRAM* dram, int num_banks, const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
    m_dram = dram;
    m_num_banks = num_banks;
    if (num_banks <= 0) m_num_banks = 1;
    
    m_dynamic_alloc_count.assign(m_num_banks, 0);
    m_bank_activity.assign(m_num_banks, 0);
    m_logger = Logging::create_logger("SmartLocalityPolicy");
    
    // FIXED: Simple initialization without complex parameters
    m_locality_weight = 0.3;
    m_max_kv_per_bank = 3;
    m_activity_threshold_percent = 10;
    
    m_logger->info("SmartLocalityPolicy initialized with {} banks", m_num_banks);
    
    // Use the setter logic to initialize weights
    set_static_weight_mapping(mapping);
  }

  void set_static_weight_mapping(const std::map<int, std::unordered_set<uint64_t>>& mapping) override {
      m_static_weight_mapping = mapping;
      m_static_weight_count.assign(m_num_banks, 0);
      
      int total_weight_banks = 0;
      int max_weight_count = 0;
      
      for (const auto& [bank_id, weight_addrs] : m_static_weight_mapping) {
        if (bank_id >= 0 && bank_id < m_num_banks) {
          int weight_count = weight_addrs.size();
          m_static_weight_count[bank_id] = weight_count;
          total_weight_banks++;
          
          if (weight_count > max_weight_count) {
            max_weight_count = weight_count;
          }
        }
      }
      
      // Calculate bank activity (0-100 scale)
      if (max_weight_count > 0) {
        for (int i = 0; i < m_num_banks; i++) {
          m_bank_activity[i] = (m_static_weight_count[i] * 100) / max_weight_count;
        }
      }
      
      m_logger->info("Weight mapping updated: {} banks have static weights", total_weight_banks);
      if (max_weight_count > 0) {
        m_logger->info("Max weight count: {}, Activity normalized to 0-100 scale", max_weight_count);
      }
      
      // Log weight banks
      for (int i = 0; i < m_num_banks; i++) {
        if (m_static_weight_count[i] > 0) {
          m_logger->info("  Bank {}: {} weights (activity: {}%)", 
                        i, m_static_weight_count[i], m_bank_activity[i]);
        }
      }
  }

  int allocate_kv_cache_bank(size_t kv_cache_size, int token_id) override {
    // SIMPLIFIED STRATEGY: Hybrid of ContentionAware and BankPartitioning
    
    // Step 1: Try to find banks without weights first
    std::vector<int> candidate_banks;
    for (int bank_id = 0; bank_id < m_num_banks; bank_id++) {
      if (m_static_weight_count[bank_id] == 0) {
        candidate_banks.push_back(bank_id);
      }
    }
    
    // Step 2: If all banks have weights, use all banks
    if (candidate_banks.empty()) {
      for (int bank_id = 0; bank_id < m_num_banks; bank_id++) {
        candidate_banks.push_back(bank_id);
      }
    }
    
    // Step 3: Score candidate banks
    int best_bank = candidate_banks[0];
    double best_score = std::numeric_limits<double>::max();
    
    for (int bank_id : candidate_banks) {
      double score = 0.0;
      
      // Penalty for having weights (conflict avoidance)
      score += m_static_weight_count[bank_id] * 100.0;
      
      // Penalty for existing allocations (spread out)
      score += m_dynamic_alloc_count[bank_id] * 10.0;
      
      // Bonus for moderate activity (locality)
      if (m_bank_activity[bank_id] >= 20 && m_bank_activity[bank_id] <= 80) {
        score -= 50.0 * m_locality_weight; // Negative is better
      }
      
      if (score < best_score) {
        best_score = score;
        best_bank = bank_id;
      }
    }
    
    // Record allocation
    m_token_to_bank[token_id] = best_bank;
    m_dynamic_alloc_count[best_bank]++;
    m_total_allocations++;
    
    // Check for conflict
    bool has_conflict = has_bank_conflict(best_bank);
    if (has_conflict) {
      m_total_conflicts++;
    }
    
    // Log first 10 allocations
    if (m_total_allocations <= 10) {
      m_logger->info("Allocation #{}: Token {} -> Bank {} (weights={}, activity={}%, existing={}, conflict={}, score={:.1f})",
                    m_total_allocations, token_id, best_bank,
                    m_static_weight_count[best_bank], m_bank_activity[best_bank],
                    m_dynamic_alloc_count[best_bank], has_conflict ? "YES" : "NO", best_score);
    }
    
    return best_bank;
  }

  int get_kv_cache_bank(int token_id) override {
    auto it = m_token_to_bank.find(token_id);
    return (it != m_token_to_bank.end()) ? it->second : -1;
  }

  bool has_bank_conflict(int bank_id) override {
    if (bank_id < 0 || bank_id >= m_num_banks) return false;
    return m_static_weight_count[bank_id] > 0;
  }

  std::map<std::string, int64_t> get_stats() override {
    // Calculate statistics
    int banks_with_kv = 0;
    int banks_with_weights = 0;
    int total_kv_allocations = 0;
    
    for (int i = 0; i < m_num_banks; i++) {
      if (m_dynamic_alloc_count[i] > 0) {
        banks_with_kv++;
        total_kv_allocations += m_dynamic_alloc_count[i];
      }
      if (m_static_weight_count[i] > 0) {
        banks_with_weights++;
      }
    }
    
    double avg_kv_per_bank = banks_with_kv > 0 ? 
        static_cast<double>(total_kv_allocations) / banks_with_kv : 0.0;
    
    // Log distribution
    m_logger->info("=== SmartLocality Final Distribution ===");
    m_logger->info("Banks with KV cache: {}", banks_with_kv);
    m_logger->info("Banks with weights: {}", banks_with_weights);
    m_logger->info("Average KV per used bank: {:.2f}", avg_kv_per_bank);
    
    // Log top 10 banks by KV allocations
    std::vector<std::pair<int, int>> bank_allocations;
    for (int i = 0; i < m_num_banks; i++) {
      if (m_dynamic_alloc_count[i] > 0) {
        bank_allocations.push_back({i, m_dynamic_alloc_count[i]});
      }
    }
    
    std::sort(bank_allocations.begin(), bank_allocations.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    m_logger->info("Top banks by KV allocations:");
    for (int i = 0; i < std::min(10, (int)bank_allocations.size()); i++) {
      int bank = bank_allocations[i].first;
      int allocs = bank_allocations[i].second;
      m_logger->info("  Bank {}: {} KV caches (weights={}, activity={}%)",
                    bank, allocs, m_static_weight_count[bank], m_bank_activity[bank]);
    }
    
    return {
      {"total_allocations", m_total_allocations}, 
      {"total_conflicts", m_total_conflicts},
      {"weight_banks", static_cast<int64_t>(banks_with_weights)},
      {"kv_banks", static_cast<int64_t>(banks_with_kv)},
      {"avg_kv_per_bank", static_cast<int64_t>(avg_kv_per_bank * 100)} // Store as integer percentage
    };
  }
  
  void reset_stats() override {
    m_total_allocations = 0;
    m_total_conflicts = 0;
    std::fill(m_dynamic_alloc_count.begin(), m_dynamic_alloc_count.end(), 0);
  }
};

} // namespace PIM
} // namespace Ramulator

#endif // KV_CACHE_POLICY_H