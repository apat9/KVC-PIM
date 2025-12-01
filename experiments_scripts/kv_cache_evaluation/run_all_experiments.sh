#!/bin/bash
# Run all KV cache evaluation experiments

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$PROJECT_ROOT/exp_results/kv_cache_evaluation"

echo "=========================================="
echo "KV Cache Evaluation Experiments"
echo "=========================================="

# Create results directory
mkdir -p "$RESULTS_DIR"

# Check if we have the new SmartLocality policy
KV_POLICY_H="$PROJECT_ROOT/include/pim_codegen/kv_cache_policy.h"
HAS_SMART_LOCALITY=$(grep -c "SmartLocalityPolicy" "$KV_POLICY_H" 2>/dev/null || echo "0")

# Run baseline
echo ""
echo "1. Running Baseline (Naive Policy)..."
if [ -f "$SCRIPT_DIR/run_baseline.sh" ]; then
    bash "$SCRIPT_DIR/run_baseline.sh"
else
    echo "   Warning: run_baseline.sh not found"
fi

# Run bank partitioning
echo ""
echo "2. Running Bank Partitioning Policy..."
if [ -f "$SCRIPT_DIR/run_bank_partitioning.sh" ]; then
    bash "$SCRIPT_DIR/run_bank_partitioning.sh"
else
    echo "   Warning: run_bank_partitioning.sh not found"
fi

# Run contention-aware
echo ""
echo "3. Running Contention-Aware Policy..."
if [ -f "$SCRIPT_DIR/run_contention_aware.sh" ]; then
    bash "$SCRIPT_DIR/run_contention_aware.sh"
else
    echo "   Warning: run_contention_aware.sh not found"
fi

# Run SmartLocality if available
if [ "$HAS_SMART_LOCALITY" -gt 0 ]; then
    echo ""
    echo "4. Running SmartLocality Policy..."
    if [ -f "$SCRIPT_DIR/run_smart_locality.sh" ]; then
        bash "$SCRIPT_DIR/run_smart_locality.sh"
    else
        echo "   Creating and running SmartLocality test..."
        # Create a quick test
        bash -c "$SCRIPT_DIR/create_smart_locality_test.sh"
    fi
else
    echo ""
    echo "4. SmartLocality Policy: Not available (add to kv_cache_policy.h)"
fi

echo ""
echo "=========================================="
echo "All experiments complete!"
echo "=========================================="
echo ""

# Function to extract cycles with fallback
extract_cycles() {
    local dir="$1"
    local file="$RESULTS_DIR/$dir/simulation_output.log"
    if [ -f "$file" ]; then
        grep "memory_system_cycles" "$file" | tail -1 | awk '{print $2}'
    else
        echo "N/A"
    fi
}

# Function to extract conflicts with fallback
extract_conflicts() {
    local dir="$1"
    local file="$RESULTS_DIR/$dir/simulation_output.log"
    if [ -f "$file" ]; then
        grep "total_conflicts" "$file" | tail -1 | awk '{print $2}'
    else
        echo "N/A"
    fi
}

echo "Memory System Cycles Summary:"
echo "----------------------------"

# Baseline
BASELINE_CYCLES=$(extract_cycles "baseline")
BASELINE_CONFLICTS=$(extract_conflicts "baseline")
echo "  Baseline (Naive):           ${BASELINE_CYCLES} cycles (conflicts: ${BASELINE_CONFLICTS})"

# Bank Partitioning
BANK_PART_CYCLES=$(extract_cycles "bank_partitioning")
BANK_PART_CONFLICTS=$(extract_conflicts "bank_partitioning")
echo "  Bank Partitioning:          ${BANK_PART_CYCLES} cycles (conflicts: ${BANK_PART_CONFLICTS})"

# Contention Aware
CONTENTION_CYCLES=$(extract_cycles "contention_aware")
CONTENTION_CONFLICTS=$(extract_conflicts "contention_aware")
echo "  Contention-Aware:           ${CONTENTION_CYCLES} cycles (conflicts: ${CONTENTION_CONFLICTS})"

# SmartLocality (if exists)
if [ "$HAS_SMART_LOCALITY" -gt 0 ]; then
    # Find the latest smart_locality directory
    LATEST_SMART_DIR=$(find "$RESULTS_DIR" -type d -name "smart_locality_*" | sort -r | head -1)
    if [ -n "$LATEST_SMART_DIR" ]; then
        SMART_CYCLES=$(grep "memory_system_cycles" "$LATEST_SMART_DIR/smart_locality.log" 2>/dev/null | tail -1 | awk '{print $2}' || echo "N/A")
        SMART_CONFLICTS=$(grep "total_conflicts" "$LATEST_SMART_DIR/smart_locality.log" 2>/dev/null | tail -1 | awk '{print $2}' || echo "N/A")
        echo "  SmartLocality:             ${SMART_CYCLES} cycles (conflicts: ${SMART_CONFLICTS})"
    fi
fi

echo ""
echo "=========================================="
echo "Performance Comparison"
echo "=========================================="

# Calculate improvements if we have baseline
if [ "$BASELINE_CYCLES" != "N/A" ] && [[ "$BASELINE_CYCLES" =~ ^[0-9]+$ ]]; then
    baseline=$BASELINE_CYCLES
    
    echo ""
    echo "Improvements vs Baseline:"
    echo "-------------------------"
    
    # Bank Partitioning
    if [[ "$BANK_PART_CYCLES" =~ ^[0-9]+$ ]]; then
        improvement=$(echo "scale=2; (($baseline - $BANK_PART_CYCLES) / $baseline) * 100" | bc 2>/dev/null || echo "0")
        if [ $(echo "$improvement > 0" | bc 2>/dev/null || echo "0") -eq 1 ]; then
            sign="+"
        else
            sign=""
        fi
        echo "  Bank Partitioning:    ${sign}${improvement}%"
    fi
    
    # Contention-Aware
    if [[ "$CONTENTION_CYCLES" =~ ^[0-9]+$ ]]; then
        improvement=$(echo "scale=2; (($baseline - $CONTENTION_CYCLES) / $baseline) * 100" | bc 2>/dev/null || echo "0")
        if [ $(echo "$improvement > 0" | bc 2>/dev/null || echo "0") -eq 1 ]; then
            sign="+"
        else
            sign=""
        fi
        echo "  Contention-Aware:     ${sign}${improvement}%"
    fi
    
    # SmartLocality
    if [[ "$SMART_CYCLES" =~ ^[0-9]+$ ]]; then
        improvement=$(echo "scale=2; (($baseline - $SMART_CYCLES) / $baseline) * 100" | bc 2>/dev/null || echo "0")
        if [ $(echo "$improvement > 0" | bc 2>/dev/null || echo "0") -eq 1 ]; then
            sign="+"
        else
            sign=""
        fi
        echo "  SmartLocality:        ${sign}${improvement}%"
    fi
fi

echo ""
echo "Results are in: exp_results/kv_cache_evaluation/"
echo ""

# Generate a summary CSV file
SUMMARY_CSV="$RESULTS_DIR/summary_$(date +"%Y%m%d_%H%M%S").csv"
echo "Policy,Cycles,Conflicts,Improvement_vs_Baseline" > "$SUMMARY_CSV"
echo "Baseline,$BASELINE_CYCLES,$BASELINE_CONFLICTS,0%" >> "$SUMMARY_CSV"
echo "BankPartitioning,$BANK_PART_CYCLES,$BANK_PART_CONFLICTS," >> "$SUMMARY_CSV"
echo "ContentionAware,$CONTENTION_CYCLES,$CONTENTION_CONFLICTS," >> "$SUMMARY_CSV"
if [ -n "$SMART_CYCLES" ] && [ "$SMART_CYCLES" != "N/A" ]; then
    echo "SmartLocality,$SMART_CYCLES,$SMART_CONFLICTS," >> "$SUMMARY_CSV"
fi

echo "Summary CSV saved to: $SUMMARY_CSV"