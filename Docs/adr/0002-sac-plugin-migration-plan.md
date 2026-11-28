---
status: proposed
---

# Plan: migrate the D3 chase adaptation experiment onto the Schola plugin backend

Context: the D3 adaptation evidence (`rightturn` → `corner`, five pre-registered
pass criteria) ran against the example-owned prototype (`OnlineSACLearner`,
`ChaseSession`), not the finished plugin backend (`URLToolsNPCPolicy` in
`Source/ScholaRLTools`). This plan reproduces the same kind of adaptation claim
through the real plugin instead. See [ADR 0001](./0001-host-sac-migration-in-nativelearning.md)
for why `Examples/NativeLearning` is the host, and the repo's `GLOSSARY.md` for
the resolved terms used below (Profile, Driving loop, Frozen, Chase task).

## Steps

1. **Describe the shape.** Add a new profile to
   `Examples/NativeLearning/Config/ScholaRLTools.json`: 10 observations,
   2 actions, SAC, carrying the tuned D3 learner structure (3x64 ReLU actor/
   critic, batch 32, replay 60000, discount 0.99, target tau 0.005, adaptive
   entropy) and runtime settings (`GradientSteps=8`, `TrainFrequency=1`,
   matching `updates_per_move=8`; `LearningStarts=1000`, matching
   `warmup_experiences`; `QueueCapacity=4096`). `catch_bonus=10` is reward
   shaping for the task itself, not a learner setting — it belongs in step 3's
   reward code, not this profile.
2. **Compile it.** Run the project build so the generator commandlet compiles a
   learner for that profile before anything can call `Init`.
3. **Build the minimal scene.** In `NativeLearning`: a `GameMode` and two
   simple actors (chaser, runner) spawning basic engine shapes in C++ — no
   hand-authored map content. A HUD shows at most 4-5 numbers (catch rate,
   policy version, learning stats) in a plain aligned list, not a styled
   dashboard — the HUD's job is diagnostic truthfulness, not polish.
4. **Write the driving loop.** Throwaway, experiment-scoped code: each tick,
   observe -> `RequestDecision` -> `TryGetDecision` -> apply (through one
   isolated "apply action" function, kept separate so player input can replace
   just that piece later) -> score reward -> `SubmitExperience`.
5. **Write the eval/comparison script.** Also throwaway: `CaptureSnapshot` the
   current policy, then run 100 fixed-seed rounds through both the live policy
   (`SetLearningEnabled(false)` + `SetInferenceMode(Deterministic)` together,
   reproducing the old combined "frozen" behavior) and the snapshot, counting
   catches/steps. This replaces what `ChaseSession::Evaluate()` used to do
   automatically.
6. **Prove it's real.** (a) grep the new files for zero
   `OnlineSACLearner`/`ChaseSession`/RLtools includes; (b) log the plugin's
   real profile id and RLtools revision string from `GetLearningStats`/
   `GetCapabilities` once, as evidence it is genuinely the plugin path.
7. **Re-score the five pass criteria** against this run and report the result
   as-is, same discipline as the D3 evidence.

## Explicitly out of scope for this plan

- Player (WASD) control of the runner/chaser — deferred; step 4's isolated
  apply-action function exists specifically so this can be added later without
  rewiring the rest.
- A reusable Schola stepper/coordinator for the observe/decide/submit loop —
  that is unassigned team work (the spec's live-agent/environment coordinators);
  step 4's driving loop stays throwaway on purpose.
- Deleting the old prototype (`chase_sim`, `ChaseSession`, `OnlineSACLearner`) —
  kept as a faster-iterating reference tool, documented as superseded.
- Visual polish of the scene/HUD beyond the minimal readout in step 3.
