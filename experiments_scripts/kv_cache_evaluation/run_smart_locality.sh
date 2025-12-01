#!/bin/bash
# SmartLocality Policy Evaluation Script - Simple folder output
# Tests the new SmartLocality policy that balances conflict avoidance with bank locality

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SIMULATOR_BIN="$PROJECT_ROOT/simulator/build/ramulator2"

# Configuration - MATCHING OTHER TESTS
MODEL="llama3-8B-128"
NUM_TOKENS=256
GROUP_TXT="$PROJECT_ROOT/Result/Group.txt"
CONFIG_FILE="$PROJECT_ROOT/simulator/hbmpim_config.yaml"
OUTPUT_DIR="$PROJECT_ROOT/exp_results/kv_cache_evaluation/smart_locality"  # Simple folder name

mkdir -p "$OUTPUT_DIR"

echo "=========================================================="
echo "SMART LOCALITY POLICY EVALUATION"
echo "=========================================================="
echo "Model: $MODEL"
echo "Tokens: $NUM_TOKENS"
echo "Output: $OUTPUT_DIR"
echo "=========================================================="

# Run SmartLocality with default parameters
echo ""
echo "Running SmartLocality Policy..."
LOG_FILE="$OUTPUT_DIR/simulation_output.log"

$SIMULATOR_BIN \
  --config_file "$CONFIG_FILE" \
  --param "Frontend.impl=PimTraceKVAware" \
  --param "Frontend.path=$GROUP_TXT" \
  --param "Frontend.enable_kv_cache=true" \
  --param "Frontend.num_tokens=$NUM_TOKENS" \
  --param "Frontend.KVCachePolicy.impl=SmartLocality" \
  --param "Frontend.KVCachePolicy.locality_weight=0.3" \
  --param "Frontend.KVCachePolicy.max_kv_per_bank=3" \
  --param "Frontend.KVCachePolicy.activity_threshold_percent=10" \
  --param "Frontend.PimCodeGen.alloc_method=new" \
  > "$LOG_FILE" 2>&1

# Extract metrics in same format as other tests
echo ""
echo "Extracting metrics..."
METRICS_FILE="$OUTPUT_DIR/metrics.txt"

echo "=== Simulation Results ===" > "$METRICS_FILE"
grep "memory_system_cycles" "$LOG_FILE" | tail -1 >> "$METRICS_FILE" 2>/dev/null || echo "memory_system_cycles: NOT_FOUND" >> "$METRICS_FILE"
grep "total_conflicts" "$LOG_FILE" >> "$METRICS_FILE" 2>/dev/null || echo "total_conflicts: NOT_FOUND" >> "$METRICS_FILE"
grep "KV Cache Policy Statistics" -A 10 "$LOG_FILE" >> "$METRICS_FILE" 2>/dev/null || echo "No policy stats found" >> "$METRICS_FILE"

# Show results
echo ""
echo "Results:"
echo "---------"
grep "memory_system_cycles" "$METRICS_FILE"
grep "total_conflicts" "$METRICS_FILE"

echo ""
echo "SmartLocality evaluation complete!"
echo "Results saved to: $OUTPUT_DIR/"
echo "=========================================================="