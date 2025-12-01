#!/bin/bash
# Bank Partitioning Policy evaluation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SIMULATOR_BIN="$PROJECT_ROOT/simulator/build/ramulator2"

# Configuration
MODEL="llama3-8B-128"
NUM_TOKENS=512
STATIC_WEIGHT_TRACE="$PROJECT_ROOT/Result/Group.txt"
OUTPUT_DIR="$PROJECT_ROOT/exp_results/kv_cache_evaluation/bank_partitioning"
KV_CACHE_BANKS_COUNT=4  # Reserve 4 banks for KV cache

mkdir -p "$OUTPUT_DIR"

echo "Running Bank Partitioning Policy Evaluation"
echo "Model: $MODEL"
echo "Tokens: $NUM_TOKENS"
echo "Reserved KV Cache Banks: $KV_CACHE_BANKS_COUNT"
echo "Output: $OUTPUT_DIR"

# Run simulation with bank partitioning policy
$SIMULATOR_BIN \
  --config_file "$PROJECT_ROOT/simulator/hbmpim_config.yaml" \
  --param "Frontend.impl=PimTraceKVAware" \
  --param "Frontend.path=$STATIC_WEIGHT_TRACE" \
  --param "Frontend.enable_kv_cache=true" \
  --param "Frontend.static_weight_trace_path=$STATIC_WEIGHT_TRACE" \
  --param "Frontend.num_tokens=$NUM_TOKENS" \
  --param "Frontend.KVCachePolicy.impl=BankPartitioning" \
  --param "Frontend.KVCachePolicy.kv_cache_banks_count=$KV_CACHE_BANKS_COUNT" \
  --param "Frontend.PimCodeGen.alloc_method=new" \
  > "$OUTPUT_DIR/simulation_output.log" 2>&1

# Extract metrics
grep "memory_system_cycles" "$OUTPUT_DIR/simulation_output.log" | tail -1 > "$OUTPUT_DIR/metrics.txt"
grep "total_conflicts" "$OUTPUT_DIR/simulation_output.log" >> "$OUTPUT_DIR/metrics.txt" || true
grep "KV Cache Policy Statistics" -A 10 "$OUTPUT_DIR/simulation_output.log" >> "$OUTPUT_DIR/metrics.txt" || true

echo "Bank Partitioning evaluation complete. Results in $OUTPUT_DIR"

