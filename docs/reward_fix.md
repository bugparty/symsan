# FGTest Reward Calculation Fix

## Problem Description

When using `fgtest` to evaluate model-predicted execution paths, a counterintuitive issue was discovered: **providing more correct branch prediction steps resulted in lower rewards**.

## Root Cause Analysis

### 1. Multiple Branches Per Line
The original `line_to_branch` used a one-to-one mapping, but due to short-circuit evaluation, a single line of code can produce multiple symbolic branches:

```c
if (mode != 0 && mode != ' ')  // One line produces two symSanIds
```

**Fix**: Changed `line_to_branch` to `line_to_branches`, storing `std::vector<BranchMeta>`.

### 2. Z3 Solver Value Mismatch
When the model provides "hypothetical" execution paths, the Z3 solver fails because concrete values don't match path constraints:

```
value mismatch for ICmp
```

This causes `solver_unknown = true`, making it impossible to verify path reachability.

### 3. Reward Formula Deficiency
The original formula returned a fixed penalty when `solver_unknown`, ignoring how well the model's predictions matched actual observations:

```cpp
// Old code
if (unknown) {
    return -0.2;  // Ignores step correctness
}
```

## Solution

### Added Observed Branch Tracking
```cpp
std::map<int, int> observed_line_to_dir;  // line -> direction (1=taken, 0=not-taken)
```

Records actual branch directions during execution for comparison with model predictions.

### Added Step Matching Metrics
```cpp
std::tuple<double, double, double> compute_step_metrics_vs_observed(
    const std::vector<TraceStep>& steps,
    const std::map<int, int>& observed_line_to_dir);
```

Computes precision (prediction accuracy), recall (coverage), and F1 score.

### Updated Reward Formula

| Condition | Reward Formula |
|-----------|----------------|
| SAT + correct answer | `0.8 + 0.2 * path_score` |
| SAT + wrong answer | `-0.8 - 0.2 * path_score` |
| UNSAT + correct answer | `0.6 + 0.2 * path_score` |
| UNSAT + wrong answer | `-0.6 - 0.2 * path_score` |
| **Unknown** | `0.5 * status_score + 0.3 * path_score - 0.2` |

Where:
- `status_score` = 1.0 (correct answer) or -1.0 (wrong answer)
- `path_score` = F1 score (model prediction vs. actual observation)

## Validation Results

Testing with `control_temp.c`:

| Scenario | Steps | Reward |
|----------|-------|--------|
| Correct reachable | 1 | 0.396 |
| Correct reachable | 2 | 0.436 |
| Correct reachable | 3 | 0.470 |
| Wrong unreachable | 2 | -0.607 |
| Wrong unreachable | 0 | -0.600 |

✅ **Rewards now increase with more correct steps, as expected.**

## Debug Switch

Added conditional compilation macro to control debug output:

```cpp
// In fgtest.cpp line 40
// #define FGTEST_DEBUG 1  // Uncomment to enable debug output

#ifdef FGTEST_DEBUG
  #define AOUT(s) ...  // Output to cerr
  #define DOUT(s) ...  // Output with timestamp
#else
  #define AOUT(s) do {} while(0)
  #define DOUT(s) do {} while(0)
#endif
```

## Files Changed

- `/workspaces/symsan/driver/fgtest.cpp`
  - Added `FGTEST_DEBUG` macro definition
  - Changed `line_to_branch` → `line_to_branches` (vector)
  - Added `observed_line_to_dir` map
  - Added `compute_step_metrics_vs_observed()` function
  - Fixed `compute_reward()` function
