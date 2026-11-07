# Advanced NPCs for AMD Schola - Individual Contributions (D3)

Each entry records that member's D3 work. Subteams maintain their own entries.

### Jace — Adaptive NPCs: native Schola/RLtools integration

- Built the reusable lifecycle/build foundations and native SAC, TD3 and PPO
  learning implementation in Schola's plugin modules, including replay/rollout
  admission, incremental updates, actor publication and immutable snapshots
  ([#30](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/30),
  [#31](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/31)).
- Implemented the Blueprint/C++ adaptive policy and world learner service:
  action conversion and decision provenance, background construction/training,
  independent and explicit shared ownership, lifecycle controls and asynchronous
  operation status ([#35](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/35)).
- Added configuration assets and UnrealBuildTool generation so developers can
  define supported observation/action shapes and network settings, then recompile
  without editing RLtools templates
  ([#36](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/36)).
- Built the independent NativeLearning test host and ran UE 5.8.4 validation:
  40 selected automation tests across nine profiles, plus 43 generator checks.
  Documented supported behavior, reproducible commands and limitations.
- Prepared the adaptive D3 report, onboarding instructions, evidence, demo
  handoff and presentation slides. Integrated the reviewed feature branches into
  the release path while preserving the other subteams' changes. PRs #31, #35
  and #36 received teammate approval from Max Xu.

### Jason Panusopone — Adaptive NPCs: Native learning diagnostics and demo usability  

Implemented field-specific validation errors for native learner runtime settings and added regression checks for zero, negative, NaN, and infinity values. Clarified AdaptiveTag HUD labels to distinguish the active policy from the preserved learned actor. Updated freeze/resume controls to reset recent catch-rate history while preserving cumulative scores and learned weights. Verified both demo changes manually in Unreal; native tests passed. Added Unreal API error-propagation checks, which have not yet been run.

### Chloe Lee — Adaptive NPCs: checkpoint save and load

- Moved my D2 checkpoint code ([#44](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/44))
  into the plugin's native learner so it works for SAC, TD3 and PPO
  ([#45](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/45)).
- Three save modes: policy only, training state, and full resume with the replay
  buffer.
- Loading checks the file first (size, checksum, config, RLtools version) and
  refuses bad or mismatched files without touching the current learner. RLtools'
  own replay buffer loader didn't work here, so I save the buffer manually.
- Native tests that run without Unreal: 460 checks, all passing on macOS.
- Blueprint save/load nodes, Unreal tests and a short guide
  ([#46](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/46)), still
  waiting on a UE 5.8 build to test.

**Evidence:** [#45](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/45),
[#46](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/46)

### Hiu Yan (Jaela) Kwok — Shared Policy Architecture Evaluation

- Built the A–E ONNX comparison from the same seeded weights, including export,
  routing, correctness tests, profiling, and the portable C/D matrix.
- Added the Unreal C/D routing, timing, isolated runner, and aggregation. All 48
  reports passed correctness; C had lower request p50 in 45 trials and all 16
  workload medians on the tested Mac CPU environment.
- Documented the experiment, limitations, presentation results, and next step:
  compare C/D model storage and memory before gameplay integration.

**Evidence:** [PR #24](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/24),
[benchmark design and reproduction guide](../../Prototypes/SharedPolicy/benchmarks/README.md),
and [D3 slides](index.html#shared-policy-benchmark).

### Olivia Markow — Adaptive NPCs: SAC chaser adaptation and learning speed

- Built three deterministic habit runners (rightturn, clockwise, corner) that react to the chaser, plus the harder chase v2 task (board 100, 200-step rounds, relative-position observation) and an INTERCEPT baseline.
- Ran a ~40-variant, 3-seed hyperparameter sweep; set new defaults (8 updates per move, 64-wide network, catch bonus 10), raising the 20,000-move catch rate from 0.34 to 0.76.
- Designed and ran the adapt-vs-frozen test (train on rightturn, snapshot, switch to corner) with five pass criteria fixed before scoring. Result: criteria 1–2 pass on all seeds; recovery (9,708–20,346 moves) and the snapshot gap miss on some seeds, reported as-is.
- Extended chase_sim: live watch view with runner mood, five-way eval, --script sessions, a one-command build script, and 40 s / 5 min demo scripts. Traced and confirmed the snapshot is a frozen, immutable policy.
- Added the repository's first GitHub Actions workflow: builds chase_sim and runs smoke tests on PRs, with a 5-minute timeout.
- Wrote the D3 adaptation section of the product evolution report and the SAC evidence README.

**Evidence:** [PR #42](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/42), [SAC D3 evidence](adaptive-npc-evidence.md#sac-chase-adaptation), [demo video](https://drive.google.com/file/d/1J_AV8wG49tRTUB4X1vTzty05bYBZ5JJi/view?usp=sharing)


### Clementine Yang — Shared Policy Unreal Integration

- **PR #32 – macOS ARM64 Support:** Added macOS build scripts and dependency configuration for Schola. Successfully built gRPC dependencies and compiled the BackgroundNPCsEditor project on Apple Silicon using Unreal Engine 5.8.

- **PR #33 – Unreal NNE Testing:** Implemented Unreal Automation Tests for Shared Policy architectures A, C, and D using `NNERuntimeORTCpu`.

- Compared Unreal inference outputs against Python ONNX Runtime results, checking numerical accuracy and tensor shapes across different batch sizes.

- Added macOS setup instructions and documented the build and testing process. Prepared the Shared Policy D3 report, demo materials and presentation slides. Coordinated with the team to deliver presentation and demo.
  

### Prithvi Ajay

- **Automatic event subscriptions:** Extend UEventStepper to subscribe to gameplay events and trigger inference automatically.
- **Event lifecycle management:** Support multiple event subscriptions, unsubscription, and cleanup when objects are destroyed.

### Max Xu — Background NPCs, Scaling the Backend

Built CampusCrowd to demonstrate Schola’s background inference in a campus scene with up to 100,000 Mass agents. Imported campus geometry, added pedestrian routes, camera controls, performance reporting, and scripts for building, packaging, and comparing inference modes. Added CAD import tests and geometry checks. Scaled the inference queue with crowd size and made missed decision targets visible. Documented measured performance, local testing and packaging commands. Built and packaged CampusCrowd, checked demo controls, and retained logs, reports, and screenshots for a standalone 20,000-agent run at 59.98 FPS and 199,922 decisions/s. [D3 validation evidence](../../Examples/CampusCrowd/Evidence/d3-validation-2026-11-05/README.md). Produced a demo video showing **20,000 agents making 200,000 decisions per second at 60 FPS**. [Watch the CampusCrowd demo](https://utoronto-my.sharepoint.com/:v:/g/personal/maxh_xu_mail_utoronto_ca/IQDEpcI8CITpT6vs0vv20yCwAU4lH9gFIKJzbEZhq47R4p8?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=R3tV6g).

[Implementation](../../Examples/CampusCrowd/README.md) · [Recorded results](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance). [Merged PR #25](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/25).
