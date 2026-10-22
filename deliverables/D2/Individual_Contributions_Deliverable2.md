# Individual Contributions - Deliverable 2

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

**Role:** Shared Policy NPCs — Unreal Engine Integration

- **Per-NPC Head Bindings (PR #7):** Implemented NPC-to-head configuration and validation, including 14 Unreal automation tests.
- **Shared Policy Stepper:** Designed `ISharedPolicy` and implemented `USharedPolicyStepper` to collect observations, perform batched head-aware inference routing, and dispatch actions to the correct NPCs.
- Added 9 automation tests covering routing, validation, and failure handling.
- Verified C++ compilation. Unreal automation tests and real ONNX/NNE integration remain pending.

**Evidence:**
- [PR #7 — Per-NPC Head Bindings](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/7)
- [Shared Policy Stepper Implementation](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/tree/feature/shared-policy-stepper)
