#ifndef STATIC_WEIGHT_LOADER_H
#define STATIC_WEIGHT_LOADER_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <sstream>

#include "base/base.h"

namespace Ramulator {
namespace PIM {

class StaticWeightLoader {
public:
  static std::map<int, std::unordered_set<uint64_t>> extract_weight_banks(
      const std::string& trace_path_str, int num_banks) {
    
    std::map<int, std::unordered_set<uint64_t>> weight_map;
    std::ifstream trace_file(trace_path_str);
    
    if (!trace_file.is_open()) {
      std::cerr << "[StaticWeightLoader] Warning: Could not open trace file: " << trace_path_str << std::endl;
      return weight_map;
    }

    std::string line;
    while (std::getline(trace_file, line)) {
      if (line.empty()) continue;

      // Skip metadata lines (Problem, Dilation, Loops, etc.)
      // We only care about explicit Write/Read commands or Kernel ops if they contain addresses
      // Note: Group.txt is often a High-Level Trace (Macro). 
      // If it doesn't contain raw "W" commands, this loader might return empty, which is safe (just no static awareness).
      
      std::stringstream ss(line);
      std::string token;
      ss >> token;

      // Only parse lines starting with R (Read) or W (Write)
      if (token == "W" || token == "R") {
        std::string addr_str;
        if (ss >> addr_str) {
          try {
            // Address string might be comma separated "0,0,0,1,..."
            // We need to parse it to find the Bank ID
            std::vector<std::string> tokens;
            std::stringstream addr_ss(addr_str);
            std::string segment;
            while(std::getline(addr_ss, segment, ',')) {
                tokens.push_back(segment);
            }

            // Ramulator HBM usually has Bank at index 3 or 1 depending on mapping
            // AddrVec: [Chan, Rank, BG, Bank, Row, Col] -> Bank is often index 3
            // Let's assume index 1 for simple PIM traces, or check size
            
            if (tokens.size() >= 2) {
                // Safely parse the bank ID
                int bank_id = std::stoi(tokens[1]); 
                
                // Construct a raw address signature (row + col)
                uint64_t raw_addr = 0;
                // Just use the first few tokens as a unique signature
                if (tokens.size() > 4) raw_addr = std::stoull(tokens[4]); 

                if (bank_id >= 0 && bank_id < num_banks) {
                    weight_map[bank_id].insert(raw_addr);
                }
            }
          } catch (...) {
            // Ignore parsing errors for individual lines
            continue;
          }
        }
      }
    }
    
    trace_file.close();
    return weight_map;
  }
};

} // namespace PIM
} // namespace Ramulator

#endif // STATIC_WEIGHT_LOADER_H