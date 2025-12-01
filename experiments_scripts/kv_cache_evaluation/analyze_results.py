#!/usr/bin/env python3
"""
Analyze KV cache evaluation results and generate comparison metrics
"""

import os
import re
import json
import pandas as pd
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.parent
RESULTS_DIR = PROJECT_ROOT / "exp_results" / "kv_cache_evaluation"

def extract_metrics(log_file):
    """Extract metrics from simulation log file"""
    metrics = {}
    
    if not os.path.exists(log_file):
        return metrics
    
    with open(log_file, 'r') as f:
        content = f.read()
    
    # Extract cycles
    cycle_match = re.search(r'memory_system_cycles:\s*(\d+)', content)
    if cycle_match:
        metrics['total_cycles'] = int(cycle_match.group(1))
    
    # Extract conflicts
    conflict_match = re.search(r'total_conflicts:\s*(\d+)', content)
    if conflict_match:
        metrics['total_conflicts'] = int(conflict_match.group(1))
    
    # Extract KV cache policy stats
    kv_stats_match = re.search(r'KV Cache Policy Statistics:(.*?)(?=\n\n|\Z)', content, re.DOTALL)
    if kv_stats_match:
        kv_stats_text = kv_stats_match.group(1)
        for line in kv_stats_text.split('\n'):
            match = re.search(r'(\w+):\s*(\d+)', line)
            if match:
                metrics[f"kv_{match.group(1)}"] = int(match.group(2))
    
    # Extract bank conflict stats
    conflict_stats_match = re.search(r'Bank Conflict Statistics:(.*?)(?=\n\n|\Z)', content, re.DOTALL)
    if conflict_stats_match:
        conflict_stats_text = conflict_stats_match.group(1)
        for line in conflict_stats_text.split('\n'):
            match = re.search(r'(\w+):\s*(\d+)', line)
            if match:
                metrics[f"conflict_{match.group(1)}"] = int(match.group(2))
    
    return metrics

def main():
    policies = ['baseline', 'bank_partitioning', 'contention_aware']
    results = []
    
    for policy in policies:
        log_file = RESULTS_DIR / policy / "simulation_output.log"
        metrics = extract_metrics(log_file)
        metrics['policy'] = policy
        results.append(metrics)
    
    # Create DataFrame
    df = pd.DataFrame(results)
    
    # Calculate throughput (tokens/second) if we have cycles
    if 'total_cycles' in df.columns:
        # Assume clock frequency (need to get from config)
        # For HBM3, typical tCK is around 1.25ns
        tCK_ns = 1.25
        df['total_time_ns'] = df['total_cycles'] * tCK_ns
        df['total_time_seconds'] = df['total_time_ns'] / 1e9
        df['throughput_tokens_per_sec'] = 512 / df['total_time_seconds']  # Assuming 512 tokens
    
    # Save results
    output_file = RESULTS_DIR / "comparison_results.csv"
    df.to_csv(output_file, index=False)
    print(f"Results saved to {output_file}")
    
    # Print summary
    print("\n=== Summary ===")
    print(df.to_string())
    
    # Calculate improvements
    if 'baseline' in df['policy'].values:
        baseline_cycles = df[df['policy'] == 'baseline']['total_cycles'].values[0]
        baseline_conflicts = df[df['policy'] == 'baseline']['total_conflicts'].values[0] if 'total_conflicts' in df.columns else 0
        
        print("\n=== Improvements vs Baseline ===")
        for policy in ['bank_partitioning', 'contention_aware']:
            if policy in df['policy'].values:
                policy_row = df[df['policy'] == policy]
                if not policy_row.empty:
                    cycles = policy_row['total_cycles'].values[0]
                    conflicts = policy_row['total_conflicts'].values[0] if 'total_conflicts' in policy_row.columns else 0
                    
                    cycle_improvement = ((baseline_cycles - cycles) / baseline_cycles) * 100
                    conflict_reduction = ((baseline_conflicts - conflicts) / baseline_conflicts * 100) if baseline_conflicts > 0 else 0
                    
                    print(f"{policy}:")
                    print(f"  Cycle reduction: {cycle_improvement:.2f}%")
                    print(f"  Conflict reduction: {conflict_reduction:.2f}%")

if __name__ == "__main__":
    main()

