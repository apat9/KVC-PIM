#ifndef BANK_CONFLICT_TRACKER_H
#define BANK_CONFLICT_TRACKER_H

#include <map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "base/base.h"

namespace Ramulator {

/**
 * @brief Tracks bank conflicts between static weights and dynamic KV cache
 * 
 * This class monitors memory requests to detect when static weight operations
 * and KV cache operations access the same bank simultaneously, causing conflicts.
 */
class BankConflictTracker {
public:
  struct ConflictEvent {
    int bank_id;
    uint64_t cycle;
    std::string conflict_type;  // "weight_kv", "kv_weight", etc.
  };

private:
  // Map from bank_id to set of addresses currently in use by static weights
  std::map<int, std::unordered_set<uint64_t>> m_weight_bank_usage;
  
  // Map from bank_id to set of addresses currently in use by KV cache
  std::map<int, std::unordered_set<uint64_t>> m_kv_cache_bank_usage;
  
  // Track active requests per bank
  std::map<int, std::vector<uint64_t>> m_active_weight_requests;
  std::map<int, std::vector<uint64_t>> m_active_kv_requests;
  
  // Statistics
  int64_t m_total_conflicts;
  int64_t m_weight_kv_conflicts;  // Weight operation blocked by KV cache
  int64_t m_kv_weight_conflicts;  // KV cache operation blocked by weights
  std::vector<ConflictEvent> m_conflict_history;
  
  int m_num_banks;

public:
  BankConflictTracker(int num_banks) 
    : m_num_banks(num_banks), m_total_conflicts(0), 
      m_weight_kv_conflicts(0), m_kv_weight_conflicts(0) {
    m_weight_bank_usage.clear();
    m_kv_cache_bank_usage.clear();
    m_active_weight_requests.clear();
    m_active_kv_requests.clear();
  }

  /**
   * @brief Register a static weight operation on a bank
   */
  void register_weight_operation(int bank_id, uint64_t addr, uint64_t cycle) {
    if (bank_id < 0 || bank_id >= m_num_banks) return;
    
    m_weight_bank_usage[bank_id].insert(addr);
    m_active_weight_requests[bank_id].push_back(addr);
    
    // Check for conflicts with KV cache
    if (m_kv_cache_bank_usage.find(bank_id) != m_kv_cache_bank_usage.end() &&
        !m_kv_cache_bank_usage[bank_id].empty()) {
      m_total_conflicts++;
      m_weight_kv_conflicts++;
      m_conflict_history.push_back({bank_id, cycle, "weight_kv"});
    }
  }

  /**
   * @brief Register a KV cache operation on a bank
   */
  void register_kv_cache_operation(int bank_id, uint64_t addr, uint64_t cycle) {
    if (bank_id < 0 || bank_id >= m_num_banks) return;
    
    m_kv_cache_bank_usage[bank_id].insert(addr);
    m_active_kv_requests[bank_id].push_back(addr);
    
    // Check for conflicts with static weights
    if (m_weight_bank_usage.find(bank_id) != m_weight_bank_usage.end() &&
        !m_weight_bank_usage[bank_id].empty()) {
      m_total_conflicts++;
      m_kv_weight_conflicts++;
      m_conflict_history.push_back({bank_id, cycle, "kv_weight"});
    }
  }

  /**
   * @brief Complete a weight operation (remove from active set)
   */
  void complete_weight_operation(int bank_id, uint64_t addr) {
    if (bank_id < 0 || bank_id >= m_num_banks) return;
    
    auto& active = m_active_weight_requests[bank_id];
    active.erase(std::remove(active.begin(), active.end(), addr), active.end());
    
    // If no active requests, we can optionally clear the usage set
    // But keep it for conflict detection
  }

  /**
   * @brief Complete a KV cache operation
   */
  void complete_kv_cache_operation(int bank_id, uint64_t addr) {
    if (bank_id < 0 || bank_id >= m_num_banks) return;
    
    auto& active = m_active_kv_requests[bank_id];
    active.erase(std::remove(active.begin(), active.end(), addr), active.end());
  }

  /**
   * @brief Check if a bank has potential conflicts
   */
  bool has_potential_conflict(int bank_id) const {
    if (bank_id < 0 || bank_id >= m_num_banks) return false;
    
    bool has_weights = (m_weight_bank_usage.find(bank_id) != m_weight_bank_usage.end() &&
                       !m_weight_bank_usage.at(bank_id).empty());
    bool has_kv = (m_kv_cache_bank_usage.find(bank_id) != m_kv_cache_bank_usage.end() &&
                  !m_kv_cache_bank_usage.at(bank_id).empty());
    
    return has_weights && has_kv;
  }

  /**
   * @brief Get conflict statistics
   */
  std::map<std::string, int64_t> get_stats() const {
    return {
      {"total_conflicts", m_total_conflicts},
      {"weight_kv_conflicts", m_weight_kv_conflicts},
      {"kv_weight_conflicts", m_kv_weight_conflicts}
    };
  }

  /**
   * @brief Reset statistics
   */
  void reset_stats() {
    m_total_conflicts = 0;
    m_weight_kv_conflicts = 0;
    m_kv_weight_conflicts = 0;
    m_conflict_history.clear();
  }

  /**
   * @brief Get conflict history
   */
  const std::vector<ConflictEvent>& get_conflict_history() const {
    return m_conflict_history;
  }
};

}  // namespace Ramulator

#endif  // BANK_CONFLICT_TRACKER_H

