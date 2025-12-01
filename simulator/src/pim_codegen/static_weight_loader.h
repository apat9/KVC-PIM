#ifndef STATIC_WEIGHT_LOADER_H
#define STATIC_WEIGHT_LOADER_H

#include <map>
#include <unordered_set>
#include <string>
#include <fstream>
#include <sstream>
#include "base/base.h"

namespace Ramulator {
namespace PIM {

/**
 * @brief Loads and parses OptiPIM's static weight mapping from trace files
 * 
 * OptiPIM generates trace files that contain bank assignments for static weights.
 * This class parses those traces to build a mapping from bank_id to set of weight addresses.
 */
class StaticWeightLoader {
public:
  /**
   * @brief Load static weight mapping from OptiPIM trace file
   * 
   * @param trace_file_path Path to the OptiPIM-generated trace file
   * @param num_banks Total number of banks in the system
   * @return Map from bank_id to set of weight addresses in that bank
   */
  static std::map<int, std::unordered_set<uint64_t>> load_from_trace(
      const std::string& trace_file_path, int num_banks) {
    
    std::map<int, std::unordered_set<uint64_t>> bank_to_weights;
    
    // Initialize all banks
    for (int i = 0; i < num_banks; i++) {
      bank_to_weights[i] = std::unordered_set<uint64_t>();
    }
    
    std::ifstream trace_file(trace_file_path);
    if (!trace_file.is_open()) {
      // If file doesn't exist, return empty mapping
      return bank_to_weights;
    }
    
    std::string line;
    while (std::getline(trace_file, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      
      // Parse trace format: "R/W/C <channel,bank,row,col,...>"
      // We're interested in weight operations (typically writes or compute operations)
      std::istringstream iss(line);
      std::string op;
      iss >> op;
      
      // Skip kernel definitions
      if (op == "conv2d" || op == "gemm" || op == "end") {
        continue;
      }
      
      // Parse address vector
      std::string addr_str;
      iss >> addr_str;
      
      std::vector<uint64_t> addr_vec;
      std::stringstream addr_stream(addr_str);
      std::string token;
      
      while (std::getline(addr_stream, token, ',')) {
        addr_vec.push_back(std::stoull(token));
      }
      
      // Address format: [channel, bank, row, col, ...]
      // We need at least channel and bank
      if (addr_vec.size() >= 2) {
        int channel = addr_vec[0];
        int bank = addr_vec[1];
        
        // Calculate a unique address identifier for this weight location
        // Combine channel, bank, row, col into a single identifier
        uint64_t weight_addr = 0;
        for (size_t i = 0; i < addr_vec.size() && i < 4; i++) {
          weight_addr = (weight_addr << 16) | (addr_vec[i] & 0xFFFF);
        }
        
        // Only track weight operations (not input/output)
        // Weights are typically written once and read many times
        // Look for patterns that indicate weight operations
        if (op == "W" || op == "compute" || op == "C") {
          int global_bank_id = channel * num_banks + bank;
          if (global_bank_id >= 0 && global_bank_id < num_banks) {
            bank_to_weights[global_bank_id].insert(weight_addr);
          }
        }
      }
    }
    
    trace_file.close();
    return bank_to_weights;
  }

  /**
   * @brief Load static weight mapping from JSON file (if OptiPIM exports JSON)
   * 
   * @param json_file_path Path to JSON file with weight mappings
   * @param num_banks Total number of banks
   * @return Map from bank_id to set of weight addresses
   */
  static std::map<int, std::unordered_set<uint64_t>> load_from_json(
      const std::string& json_file_path, int num_banks) {
    
    std::map<int, std::unordered_set<uint64_t>> bank_to_weights;
    
    // Initialize all banks
    for (int i = 0; i < num_banks; i++) {
      bank_to_weights[i] = std::unordered_set<uint64_t>();
    }
    
    // TODO: Implement JSON parsing if OptiPIM exports weight mappings in JSON format
    // For now, return empty mapping
    
    return bank_to_weights;
  }

  /**
   * @brief Extract weight bank mapping from OptiPIM trace by analyzing bank assignments
   * 
   * This method analyzes the trace to identify which banks contain weights
   * by looking for write operations to weight tensors.
   */
  static std::map<int, std::unordered_set<uint64_t>> extract_weight_banks(
      const std::string& trace_file_path, int num_banks) {
    
    return load_from_trace(trace_file_path, num_banks);
  }
};

}  // namespace PIM
}  // namespace Ramulator

#endif  // STATIC_WEIGHT_LOADER_H

