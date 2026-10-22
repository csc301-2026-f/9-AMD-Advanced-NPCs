# Individual Contributions - Deliverable 2

## Max

**Role:** Implemented and evaluated Schola's batched background NPC inference for Actors and MassEntity.

- Built the bounded batching scheduler with seven dispatch strategies, cancellation, expiry, and reusable buffers; added NNE CPU backends and typed observation/action encoding.
- Integrated the scheduler with the policy API, Actor `BackgroundStepper`, and `ScholaMass` entity processors. Documented the architecture and setup.
- Fixed pipelined-stepper oversubscription and stale callbacks, addressed UE 5.8 build issues, and added backend, codec, scheduler, and adapter tests, including 100,000 fault-injection requests per dispatch strategy.
- Wrote the literature review and experiment plans; built Python/C++ benchmarks comparing batching, worker dispatch, Actor/Mass processing, and CPU/GPU execution.
- Ran automation, lifecycle/memory diagnostics, and replicated benchmarks. Local reference runs sustained 31,000 decisions/s on each adapter with zero Busy rejections or expired requests; documented unresolved Actor pool growth and inconclusive rendered-performance results.

**Evidence:** [Merged PR #6](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/6). Research, benchmark scripts, and results are retained in local commit `66b25b3` on `local/tempstuff-66b25b3` and are not yet published on `main`.

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
