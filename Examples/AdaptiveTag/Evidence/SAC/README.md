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

Where each column comes from (corrected 2026-10-22, see below):
- Native, updateEvery=50: the `after 20000 steps` lines of `native-push/native-push-adapt.txt`
  (and the step-20000 rows of `native-push/push-seedN-adapt.csv`). That adapt run trained 20k
  steps on pattern 0 with the default updateEvery=50 before its 10k circle steps.
- Native diagnostic, updateEvery=1: `native-push-diagnostic-updateEvery1/native-push.txt` and its
  `push-seedN.csv`. Its console header does not print updateEvery; the folder name records it.
- Unreal: `unreal/push-seedN.csv`.

All pass, but natively with updateEvery=50 seed 2 was below 0.80 from 14k-19k steps and its
steps-to-catch (99.6) is close to the 100 limit. With updateEvery=1 (no snapshot lag) the push
learner matches Stage 1, which points at the 50-step policy lag (one run per setting).

**File-name fix.** The standalone 20k-step updateEvery=50 run used to write
`native-push/native-push.txt` and `native-push/push-seedN.csv`. The updateEvery=1 diagnostic
writes the same names, so it overwrote them. Its output was then moved to
`native-push-diagnostic-updateEvery1/`, which left `native-push/` with only the adapt and
lifecycle files. The table above now names the files its numbers actually come from.

**Re-run, updateEvery=50** (`native-push-rerun-updateEvery50/`, run on 2026-10-22 after the
learner was templated for chase v2): seeds 1/2/3 got 0.97 / 88.9, 0.96 / 94.2, 0.95 / 87.5,
all PASS. That is not the same as the column above, and the reason is that updateEvery=50 runs
are **not reproducible run to run**. The harness's `WaitForLearner(50)` returns while a
50-update round can still be in flight, so `Act` and evaluation race the snapshot swap. The parts
that don't depend on that race match the old files exactly: the step-0 evals, the step-1000
bounce evals, and the training returns before the first update. The first difference is the
step-1000 circle eval, which is when the first round starts. Chase v2 (`ChaseSession`, below)
waits for a backlog below `update_every`, so it is deterministic.

Lifecycle (`native-push/native-lifecycle.txt`, Unreal `SAC.Learner.Lifecycle`): 8/8 invalid
experiences rejected with no state change; versions 0 0 1 2 3 4 5; shutdown mid-round returns in
under 0.01 s; ~1.6M concurrent `Act` calls across 191 snapshot swaps with 0 invalid actions.

## Chase v2 live session: `chase_sim` (`ChaseSession`), 2026-10-22
The core is engine-neutral C++ in `Source/OnlineSACRuntime`: `ChaseConfig.h` (every tunable value),
`ChaseWorld`, `RunnerPatterns`, `ChaseSession`. It uses the push learner templated for 8
observations. `Tools/ChaseSim` is a console front end (`Build-ChaseSim.cmd` builds `chase_sim.exe`).
Defaults: board 100, runner speed 1 = chaser max speed 1, catch distance 3, min start distance 30,
300 steps, reward = distance closed + 50 on catch, update_every 1, lockstep.

Learning check, circle, `eval 50` (fixed starts, eval_seed 4242) before and after `train 300`
(catch rate / avg steps to catch / avg final distance):

| Seed | Before (v0) | After 300 episodes | Greedy pursuit | Random |
|---|---|---|---|---|
| 1 | 0.08 / 140.5 / 45.7 | 1.00 / 77.7 / 2.7 (v40911) | 0.40 / 89.5 / 3.5 | 0.18 / 149.3 / 35.6 |
| 2 | 0.22 / 141.5 / 41.3 | 1.00 / 70.2 / 2.7 (v39415) | same | same |
| 3 | 0.16 / 149.8 / 44.8 | 1.00 / 58.9 / 2.7 (v41245) | same | same |

Learning is visible on every seed. Greedy pursuit at equal speed mostly ends up trailing just
outside the catch distance (final distance 3.5). SAC learns to cut across the circle. 300
episodes is about 40k steps and about 170-180 s per seed (three seeds ran in parallel with a UE
build). The training lines show catch rate (last 20) going from 0.10-0.25 to 0.85-1.00 by
episode 150.

Adaptation check, seed 1: after the 300 circle episodes above, `snapshot`, `pattern square`:

| | Catch / steps / final dist on square |
|---|---|
| Snapshot = current right after the switch (v40911) | 1.00 / 149.4 / 2.8 |
| Current after 150 square episodes (v59434) | 1.00 / 155.0 / 2.6 |
| Greedy pursuit | 1.00 / 128.6 / 2.7 |
| Random | 0.18 / 145.1 / 46.1 |

The circle policy already catches every square start, so there is no dip to recover from. 150
more episodes on square did not improve it (155.0 vs 149.4 steps), and greedy pursuit is faster
on the square (its corners give pursuit a natural shortcut). This is one seed.

Other evidence in `chase_sim/`: `transcript.txt` runs every console command once, with bad
inputs (script: `scripts/transcript.txt`). `watch-invariance-a/b.txt` gives identical training
lines with and without watch/replay/eval in between. `demo-run.txt` is the 3-minute demo script
(62 s of compute). Unreal: `unreal/automation-ScholaOnlineLearning.SAC.ChaseSession.txt` (Success).
The learning runs used the build from just before a cosmetic fix to the eval table's column
width. The transcript, invariance and demo runs used the final build.

## Files
- `native/`, `native-adapt/`, `native-push/`, `native-push-diagnostic-updateEvery1/`, `native-push-rerun-updateEvery50/`: per-seed CSV curves and console output (see the file-name fix under Stage 2).
- `chase_sim/`: chase v2 console runs, their command scripts (`scripts/`) and `log` CSVs.
- `unreal/`: per-seed CSVs from the automation tests and the `LogAutomationController` lines.

CSV columns: `seed, step, training_pattern, bounce_*, circle_*` (catch rate, mean steps to catch,
mean return on the 100 evaluation episodes), `train_return_last10, seconds`.
