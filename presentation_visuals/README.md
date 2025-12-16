# Presentation Visuals Guide

This directory contains 7 key visuals for your KVC-PIM research presentation.

## Visual Files

### 1. `visual1_conflict_diagram.png/pdf`
**Purpose**: Attention-getter / Problem illustration  
**Content**: Shows static weights and KV cache both accessing the same banks, creating conflicts  
**Use in**: Slide 1 (Attention-Getter) or Slide 3 (The Critical Blind Spot)

### 2. `visual2_optipim_process.png/pdf`
**Purpose**: Explain OptiPIM's optimization process  
**Content**: Flow diagram showing: Static Weights → ILP Solver → Optimal Bank Assignment → High Parallelism  
**Use in**: Slide 2 (The OptiPIM Success Story)

### 3. `visual3_blind_spot.png/pdf`
**Purpose**: Illustrate OptiPIM's blind spot  
**Content**: Shows what OptiPIM sees (weights) vs. what it doesn't see (KV cache), with conflict indicators  
**Use in**: Slide 3 (The Critical Blind Spot)

### 4. `visual4_framework_architecture.png/pdf`
**Purpose**: Show the co-designed framework architecture  
**Content**: Diagram showing OptiPIM + KV Cache Policy → Integration Layer → Components → Output  
**Use in**: Slide 6 (High-Level Overview) or Slide 7 (How It Works)

### 5. `visual5_policy_comparison.png/pdf`
**Purpose**: Compare the four policies visually  
**Content**: Four subplots showing each policy's strategy (Naive, Bank Partitioning, Contention-Aware, Smart Locality)  
**Use in**: Slide 8 (The Four Policies Explained)

### 6. `visual6_results_charts.png/pdf`
**Purpose**: Present experimental results  
**Content**: Two bar charts - Bank Conflicts and Performance (Cycles) comparison across all policies  
**Use in**: Slide 11 (Key Results - Conflict Elimination) and Slide 12 (Key Results - Performance Comparison)

### 7. `visual7_before_after.png/pdf`
**Purpose**: Show the impact of the co-designed framework  
**Content**: Side-by-side comparison: BEFORE (OptiPIM alone with conflicts) vs AFTER (Co-designed framework with zero conflicts)  
**Use in**: Slide 13 (What This Means) or Conclusion slide

## File Formats

Each visual is available in:
- **PNG format** (300 DPI) - For PowerPoint, Keynote, or online presentations
- **PDF format** - For LaTeX/Beamer presentations or high-quality printing

## Color Scheme

The visuals use a consistent color scheme:
- **Red** (`#E74C3C`): Baseline/Naive policy
- **Green** (`#27AE60`): Contention-Aware policy (success)
- **Blue** (`#3498DB`): Bank Partitioning policy
- **Purple** (`#9B59B6`): Smart Locality policy
- **Orange** (`#F39C12`): Static weights
- **Teal** (`#16A085`): KV cache
- **Dark Orange** (`#E67E22`): Conflicts

## Usage Tips

1. **For PowerPoint/Keynote**: Use PNG files, they're high resolution (300 DPI)
2. **For LaTeX/Beamer**: Use PDF files for vector graphics
3. **Customization**: The Python script (`generate_presentation_visuals.py`) can be modified to adjust colors, sizes, or content
4. **Size**: All visuals are sized for standard presentation slides (16:9 or 4:3 aspect ratio)

## Regenerating Visuals

To regenerate or modify visuals:

```bash
cd /Users/aaryanpatel/KVC-PIM
python3 generate_presentation_visuals.py
```

The script will overwrite existing files in the `presentation_visuals/` directory.

## Notes

- Some Unicode characters (checkmarks, warning signs) may show warnings during generation but still render correctly
- All visuals use consistent styling and fonts
- Text is sized for readability in presentations
- Visuals are designed to be self-explanatory with clear labels

