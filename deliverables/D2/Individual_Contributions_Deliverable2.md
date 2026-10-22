# Individual Contributions - Deliverable 2

## Max

**Role:** Implemented and evaluated Schola's batched background NPC inference for Actors and MassEntity.

- Built the bounded batching scheduler with seven dispatch strategies, cancellation, expiry, and reusable buffers; added NNE CPU backends and typed observation/action encoding.
- Integrated the scheduler with the policy API, Actor `BackgroundStepper`, and `ScholaMass` entity processors. Documented the architecture and setup.
- Fixed pipelined-stepper oversubscription and stale callbacks, addressed UE 5.8 build issues, and added backend, codec, scheduler, and adapter tests, including 100,000 fault-injection requests per dispatch strategy.
- Wrote the literature review and experiment plans; built Python/C++ benchmarks comparing batching, worker dispatch, Actor/Mass processing, and CPU/GPU execution.
- Ran automation, lifecycle/memory diagnostics, and replicated benchmarks. Local reference runs sustained 31,000 decisions/s on each adapter with zero Busy rejections or expired requests; documented unresolved Actor pool growth and inconclusive rendered-performance results.
- Built the `Examples/BackgroundNPCs` comparison project. It runs the same 20,000-agent Mass crowd with per-agent synchronous inference and with the background API, with a live statistics HUD and measurement/recording scripts. Measured 10.4 FPS without the API vs. the 60 FPS cap with it, at the same ~200,000 decisions/s. Wrote the README's background-agents use case and the background NPC sections of the iteration review.

**Evidence:** [Merged PR #6](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/6), [comparison example](../../Examples/BackgroundNPCs/README.md), [side-by-side recording](evidence/background-npc-side-by-side.mp4), and [measurements](evidence/background-npc-comparison.md). Research, benchmark scripts, and results are retained in local commit `66b25b3` on `local/tempstuff-66b25b3` and are not yet published on `main`.

## Jace

**Role:** Created the adaptive tag game and explored online learning integration options.

- Created the C++ Unreal tag arena with WASD runner control, two interception routes, collision-based catch/escape outcomes, and scripted runners with switchable left/right preferences.
- Explored a native alternative to the Python-backed learning direction. Built a small custom categorical actor-critic with RLtools neural networks, backpropagation, and Adam so rewards update the tagger's route probabilities during play.
- Connected scored rounds to a background training worker and published complete actor snapshots for gameplay. Kept learned weights across runner-mode changes and made the actor control each learning-mode route decision from the first round.
- Added the game display for observations, actions, rewards, probabilities, experiences, and training statistics, plus controls for freezing learning, resetting weights, selecting a fixed comparison policy, and speeding up automated rounds.
- Added repeatable native and Unreal checks, including four-seed bias reversal tests, 26 arena checks, and nine packaged learning checks covering 512 scored experiences and updates. Tuned the actor learning rate against these checks.
- Brought the prototype into `Examples/AdaptiveTag` with its map, materials, pinned RLtools headers, and portable build/run/package scripts. Verified builds, packaging, and tests from a separate fresh checkout on the development machine.
- Wrote the online learning README, iteration review, and contribution documentation, including setup instructions, scope limits, and links to recorded test evidence.

**Evidence:** [prototype PR #8](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/8), [prototype source and setup](../../Examples/AdaptiveTag/README.md), [native tests](evidence/adaptive-native-tests.txt), [packaged learning report](evidence/adaptive-packaged-test.json), and [arena report](evidence/adaptive-arena-smoke.json).

The current result is a native Unreal + RLtools proof of concept. Integration with Schola's APIs remains future work.

## Clementine

**Role:** Shared Policy NPCs — Unreal Engine Integration and D2 Demo Development

- **Per-NPC Head Bindings (PR #7):** Implemented NPC-to-head configuration, head lookup, and binding validation, including 14 Unreal automation tests.
- **Shared Policy Stepper (PR #13):** Designed the `ISharedPolicy` interface and implemented `USharedPolicyStepper` to collect NPC observations, perform a single batched policy call, and route actions to the correct NPCs based on their assigned heads.
- **Testing and Validation:** Added 9 Stepper automation tests covering head routing, observation collection, invalid bindings, and inference failures. Checked C++ compilation; Unreal automation tests and real ONNX/NNE integration remain pending.
- **Interactive Demo:** Developed a Streamlit-based UI to demonstrate real ONNX Runtime inference, per-NPC head selection, and dynamic observation inputs.
- **Documentation and Presentation:** Prepared individual contribution documentation, README, D2 iteration review content, and Shared Policy presentation materials for the D2 demonstration.

**Evidence:**
- [PR #7 — Per-NPC Head Bindings](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/7)
- [PR #13 — Shared Policy Stepper](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/13)

## Chloe

**Role:** Adaptive NPCs: checkpoint saving and loading, timing tests, and the evaluation plan for the online SAC learner.

- **Checkpoint save and load (FR-11):** Added `SaveCheckpoint` and `LoadCheckpoint` to `OnlineSACLearner`. A checkpoint stores the actor, both critics and target critics, Adam state and the training RNG. Files are checked for size, checksum and structure before loading, so a bad file is refused and the running model is left unchanged instead of crashing.
- **Checkpoint tests (test 7):** Added 10 native checks: bit-identical actor outputs after a round trip, version handling, and refusal of truncated, corrupted, renamed and missing files. All pass, and the existing learner lifecycle checks still pass.
- **Timing test (test 8):** Added a native timing harness. A decision takes about 0.006 ms against a 0.1 ms target, and a 50-step training round takes 79 ms against a 5,000 ms budget.
- **Evaluation plan (test 9):** Showed that the current circle pattern cannot demonstrate adaptation, because a policy trained on bouncing targets already catches it 100% of the time with no further training. Proposed a harder second pattern and a fixed adaptive-vs-frozen protocol.
- **Spec:** Wrote the Person D spec for the learner thread, experience queue, snapshots, frozen mode, saving format and tests 6 to 9.

All tests ran natively on macOS with clang, not yet inside Unreal.

**Evidence:** [branch `feature/adaptive-sac-checkpoint`](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/tree/feature/adaptive-sac-checkpoint) (commits `56c01b5`, `8cbe90b`), with test output in `Examples/AdaptiveTag/Evidence/SAC/native-checkpoint/` and `native-timing/`.
