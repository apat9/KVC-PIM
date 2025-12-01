# KV Cache-Aware PIM Framework Implementation

## Overview

This document describes the implementation of a KV cache-aware PIM framework that co-optimizes OptiPIM's static weight mapping with dynamic KV cache placement policies for LLM inference.

## Research Contribution

This implementation addresses the research gap identified in the proposal: **the unaddressed resource contention between static, pre-mapped model weights and dynamic, growing runtime data (i.e., the KV cache)**.

## Architecture

### Key Components

1. **KV Cache Placement Policy Interface** (`simulator/src/pim_codegen/kv_cache_policy.h`)
   - Abstract interface for KV cache placement policies
   - Three implementations:
     - `NaiveKVCachePolicy`: Round-robin baseline (unaware of static weights)
     - `BankPartitioningPolicy`: Reserves banks exclusively for KV cache
     - `ContentionAwarePolicy`: Intelligently places KV cache in banks with least static weight usage

2. **Static Weight Loader** (`simulator/src/pim_codegen/static_weight_loader.h`)
   - Parses OptiPIM-generated trace files to extract static weight bank mappings
   - Builds a map from bank_id to set of weight addresses

3. **Bank Conflict Tracker** (`simulator/src/memory_system/bank_conflict_tracker.h`)
   - Monitors memory requests to detect bank conflicts
   - Tracks conflicts between static weight operations and KV cache operations
   - Provides statistics on conflict rates

4. **KV Cache Trace Generator** (`simulator/src/frontend/impl/memory_trace/kv_cache_trace_generator.h`)
   - Generates KV cache read/write operations for LLM inference
   - Models autoregressive generation with growing KV cache

5. **Enhanced PIM Trace Frontend** (`simulator/src/frontend/impl/memory_trace/pim_trace_kv_aware.cpp`)
   - Extends base PIM trace frontend with KV cache awareness
   - Integrates KV cache policies and conflict tracking
   - Injects KV cache operations into simulation traces

## Files Changed

### New Files Created

1. **Core Framework Files:**
   - `simulator/src/pim_codegen/kv_cache_policy.h` - KV cache placement policy interface and implementations
   - `simulator/src/pim_codegen/static_weight_loader.h` - Static weight mapping loader
   - `simulator/src/memory_system/bank_conflict_tracker.h` - Bank conflict tracking
   - `simulator/src/frontend/impl/memory_trace/kv_cache_trace_generator.h` - KV cache trace generation
   - `simulator/src/frontend/impl/memory_trace/pim_trace_kv_aware.cpp` - Enhanced PIM trace frontend

2. **Evaluation Scripts:**
   - `experiments_scripts/kv_cache_evaluation/run_baseline.sh` - Baseline evaluation script
   - `experiments_scripts/kv_cache_evaluation/run_bank_partitioning.sh` - Bank partitioning evaluation
   - `experiments_scripts/kv_cache_evaluation/run_contention_aware.sh` - Contention-aware evaluation
   - `experiments_scripts/kv_cache_evaluation/run_all_experiments.sh` - Run all experiments
   - `experiments_scripts/kv_cache_evaluation/analyze_results.py` - Results analysis script

### Modified Files

1. **Build System:**
   - `simulator/src/frontend/CMakeLists.txt` - Added `pim_trace_kv_aware.cpp`
   - `simulator/src/pim_codegen/CMakeLists.txt` - Added KV cache policy headers
   - `simulator/src/memory_system/CMakeLists.txt` - Added bank conflict tracker header

## Usage

### Building the Simulator

```bash
cd simulator
mkdir -p build
cd build
cmake ..
make -j
```

### Running Experiments

#### 1. Generate OptiPIM Static Weight Mapping

First, generate the static weight mapping using OptiPIM:

```bash
./build/bin/pim-opt --data-layout-pass \
                    --trace-output-path ./Result/Group.txt \
                    --target-device-type 1 \
                    --config-arch-path ./data/hbm_pim.json \
                    --config-knobs-path ./data/knobs.json \
                    ./nn_models/llama3-8B-128/single_layers/16banks/layerinfo.*.mlir
```

#### 2. Run Baseline (Naive Policy)

```bash
cd experiments_scripts/kv_cache_evaluation
./run_baseline.sh
```

#### 3. Run Bank Partitioning Policy

```bash
./run_bank_partitioning.sh
```

#### 4. Run Contention-Aware Policy

```bash
./run_contention_aware.sh
```

#### 5. Run All Experiments

```bash
./run_all_experiments.sh
```

#### 6. Analyze Results

```bash
python analyze_results.py
```

### Configuration

The KV cache policies can be configured via YAML or command-line parameters:

```yaml
Frontend:
  impl: PimTraceKVAware
  enable_kv_cache: true
  static_weight_trace_path: "./Result/Group.txt"
  num_tokens: 512
  KVCachePolicy:
    impl: ContentionAware  # Options: Naive, BankPartitioning, ContentionAware
    kv_cache_banks_count: 4  # For BankPartitioning policy
```

## Expected Results

### Baseline (Naive Policy)
- High bank conflict rate as KV cache grows
- Throughput degrades with increasing token count
- Demonstrates the problem: static-only mapping is insufficient

### Bank Partitioning Policy
- Zero bank conflicts (by design)
- Slightly reduced OptiPIM mapping quality (fewer banks available)
- Stable throughput regardless of token count

### Contention-Aware Policy
- Significantly reduced bank conflicts
- Maintains OptiPIM's mapping quality
- Best overall throughput improvement

## Metrics Collected

1. **Total Cycles**: End-to-end latency
2. **Throughput**: Tokens per second
3. **Bank Conflicts**: Number of conflicts between weights and KV cache
4. **KV Cache Policy Statistics**: Allocation counts, conflict rates
5. **Bank Conflict Statistics**: Detailed conflict breakdown

## Next Steps for Full Evaluation

1. **Generate OptiPIM Mappings**: Run OptiPIM on attention layers of LLM models
2. **Sweep Token Counts**: Run experiments with 1, 64, 128, 256, 512 tokens
3. **Collect Metrics**: Extract cycles, conflicts, and throughput for each policy
4. **Generate Plots**: Create "Tokens Generated vs Throughput" and "Tokens Generated vs Bank Conflicts" graphs
5. **Compare Policies**: Demonstrate that new policies outperform baseline

## Integration Notes

- The implementation is designed to be non-intrusive: existing OptiPIM workflows continue to work
- KV cache policies are opt-in via the `enable_kv_cache` parameter
- Static weight mappings are loaded from OptiPIM trace files
- The framework is extensible: new policies can be added by implementing `IKVCachePolicy`

## Limitations and Future Work

1. **Static Weight Mapping Parsing**: Currently uses heuristics to identify weight operations in traces. Could be improved with explicit metadata from OptiPIM.

2. **KV Cache Size Estimation**: Currently uses fixed sizes. Should be parameterized based on model architecture.

3. **Conflict Detection**: Currently tracks potential conflicts. Could be enhanced with actual timing-aware conflict detection.

4. **Multi-Channel Support**: Current implementation assumes single channel. Should be extended for multi-channel systems.

5. **Attention Pattern Modeling**: KV cache access patterns are simplified. Real attention patterns could be more accurately modeled.

