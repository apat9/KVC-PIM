#!/bin/bash
# Run all KV cache evaluation experiments

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo "KV Cache Evaluation Experiments"
echo "=========================================="

# Run baseline
echo ""
echo "1. Running Baseline (Naive Policy)..."
bash "$SCRIPT_DIR/run_baseline.sh"

# Run bank partitioning
echo ""
echo "2. Running Bank Partitioning Policy..."
bash "$SCRIPT_DIR/run_bank_partitioning.sh"

# Run contention-aware
echo ""
echo "3. Running Contention-Aware Policy..."
bash "$SCRIPT_DIR/run_contention_aware.sh"

echo ""
echo "=========================================="
echo "All experiments complete!"
echo "=========================================="
echo ""
echo "Results are in: exp_results/kv_cache_evaluation/"
echo ""
echo "Next steps:"
echo "  1. Run analysis script: python analyze_results.py"
echo "  2. Generate plots: python plot_results.py"

