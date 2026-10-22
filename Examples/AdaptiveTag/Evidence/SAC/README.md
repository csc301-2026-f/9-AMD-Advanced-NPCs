# RLtools SAC in Unreal: evidence

Everything here was produced on 2026-10-22 on Windows, UE 5.7, MSVC 14.44, CPU only, no Python.
Pass criteria were fixed before training: see `THRESHOLDS.md` (from `baselines.txt`).

## Task
`chase` (Source/OnlineSACRuntime/Private/ChaseEnvironment.h), an RLtools environment shaped like `flag`:
10x10 board, dt 0.02, 200 steps, agent max speed 5, **agent max acceleration 20** (added),
target speed 3, catch distance 0.5, **min start distance 3** (added). Target pattern 0 = bouncing
straight lines, 1 = circles. 6 observations in [-1, 1], 2 actions in [-1, 1].
Reward = distance closed / (5 * dt) + 10 on catch; terminated = caught.
Evaluation: 100 fixed start states (eval seed 4242), deterministic (mean) SAC action.

## Baselines (pattern 0)
| Policy | Catch rate | Steps to catch |
|---|---|---|
| Random | 0.14 | 125.9 |
| Greedy pursuit | 1.00 | 66.8 |

## Stage 1: RLtools' own SAC loop (`rl::algorithms::sac::loop::core`), 20k steps
| Seed | Native catch / steps | Unreal `ScholaOnlineLearning.SAC.Chase` catch / steps |
|---|---|---|
| 1 | 0.98 / 81.3 | 0.95 / 90.4 |
| 2 | 0.99 / 85.8 | 0.86 / 96.1 |
| 3 | 0.98 / 77.2 | 0.99 / 80.3 |

Untrained: 0.05-0.07. About 73-78 s wall time (3 seeds in parallel threads). Native and Unreal
builds diverge numerically (different code generation), each is deterministic run to run.

Adaptation (evidence only, `native-adapt/`): after pattern 0 the policy already catches the
circle target 100% zero-shot, so there is no dip to recover from. 10k more steps on circles keep
catch rate 1.00 and shorten circle steps-to-catch (seeds 1/2/3: 72.0 / 65.6 / 70.0).

## Stage 2: push learner (`OnlineSACLearner`) driven by our own loop, 20k steps
Batches come from the RLtools runner's replay buffer, filled by `add()` and sampled with
`gather_batch()`; the runner's environment is never stepped.

| Seed | Native, updateEvery=50 | Native diagnostic, updateEvery=1 | Unreal `SAC.ChasePush`, updateEvery=50 |
|---|---|---|---|
| 1 | 0.86 / 95.7 | 0.98 / 74.9 | 0.94 / 86.2 |
| 2 | 0.84 / 99.6 | 1.00 / 77.5 | 0.98 / 87.2 |
| 3 | 0.96 / 78.4 | 0.97 / 88.2 | 1.00 / 81.6 |

All pass, but natively with updateEvery=50 seed 2 was below 0.80 from 14k-19k steps and its
steps-to-catch (99.6) is close to the 100 limit. With updateEvery=1 (no snapshot lag) the push
learner matches Stage 1, which points at the 50-step policy lag (one run per setting).

Lifecycle (`native-push/native-lifecycle.txt`, Unreal `SAC.Learner.Lifecycle`): 8/8 invalid
experiences rejected with no state change; versions 0 0 1 2 3 4 5; shutdown mid-round returns in
under 0.01 s; ~1.6M concurrent `Act` calls across 191 snapshot swaps with 0 invalid actions.

## Files
- `native/`, `native-adapt/`, `native-push/`, `native-push-diagnostic-updateEvery1/`: per-seed CSV curves and console output.
- `unreal/`: per-seed CSVs from the automation tests and the `LogAutomationController` lines.

CSV columns: `seed, step, training_pattern, bounce_*, circle_*` (catch rate, mean steps to catch,
mean return on the 100 evaluation episodes), `train_return_last10, seconds`.
