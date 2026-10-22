# SAC chase pass criteria (fixed before any SAC training on chase)

Written 2026-10-22 from `baselines.txt` (pattern 0 / bounce, the 100 fixed evaluation
episodes, eval seed 4242, the same start states every SAC evaluation uses):

| Policy | Catch rate | Mean steps to catch |
|---|---|---|
| Random (uniform actions) | 0.140 | 125.9 |
| Greedy pursuit | 1.000 | 66.8 |

Pass, on **each** of seeds 1, 2, 3, with the deterministic (mean) SAC action after
training on pattern 0:

1. Catch rate >= **0.80**: clearly beats random (0.14), close to greedy (1.00).
2. Mean steps to catch <= **100** (1.5 x greedy's 66.8).

These numbers are constants in `Tools/OnlineSACTests/Main.cpp` and in the Unreal test
`ScholaOnlineLearning.SAC.Chase`. They must not be loosened to make a run pass.
The pattern 0 -> 1 adaptation run is evidence only, not pass/fail.
