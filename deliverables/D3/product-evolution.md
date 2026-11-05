# Advanced NPCs for AMD Schola

This report describes the team's improvements since D2. The adaptive NPC work
turns the native learning prototypes into a configurable Schola integration.

## 1. What Needed to Improve

### Adaptive NPCs

At D2, we could demonstrate learning inside Unreal: AdaptiveTag learned a route
preference, and a separate SAC prototype learned a chase task. However, a Schola
developer could not reuse a general native backend to select an algorithm, define
their own observations and actions, and operate learning through Schola's policy
API. Task-specific learners and their example projects still supplied important
parts of the implementation. The gap mattered because adapting the prototype to
another NPC required changing learner code instead of configuring a plugin.

We identified this gap by comparing the [D2 review](../D2/iteration-02-review.md)
and prototype workflows with Schola's existing Python training integration. In
that integration, the game defines observations, actions, rewards and episode
conditions; SB3 or RLlib constructs and trains the selected algorithm. We needed
the same division of responsibilities with RLtools running locally in C++.
Success meant demonstrating this workflow through the actual Schola policy API,
including asynchronous learning and configurations beyond our example shapes.

**Shared Policy Team**： In D2, we had a working Shared Policy prototype with a shared backbone and multiple specialized heads, but it was mainly tested in Python. We needed to verify its compatibility with Unreal Engine and explore different architectures to understand their potential performance trade-offs.

### Background NPCs — Max Xu

D2 demonstrated background inference with 20,000 Mass agents in a comparison scene. It did not show how the same API would fit a large campus with buildings and pedestrian routes. This gap follows from reviewing the D2 demo’s scope; no new partner feedback is claimed. CampusCrowd makes that integration visible and tests how rendering, movement, and inference interact at larger crowd sizes.

## 2. What You Did

### Adaptive NPCs

**Developer workflow.** A developer creates a native policy in Blueprint or C++,
chooses SAC, TD3 or PPO, supplies a Schola interaction definition, and waits for
the learner to become Ready. The game requests a decision, executes the returned
action and submits the outcome. The policy exposes accepted experiences,
completed training updates and published policy versions separately, so an
accepted reward is never presented as proof that training has already completed.
Pause/resume, inference mode, episode reset, learning reset, shutdown and immutable
inference snapshots use the common adaptive API.

**Architecture.** The game owns task semantics. Schola validates and converts
observations/actions, retains decision attribution and manages policy lifecycle.
A world subsystem owns independent learners by default and schedules bounded,
incremental training on background workers. Compatible NPCs can explicitly join a
shared learner while retaining separate agent and episode identities. Releasing
one shared binding preserves the others. RLtools supplies numerical training,
optimizer state, replay/rollout storage and actor publication; the adapter reuses
these operations. Expensive construction, reconstruction and disposal run outside
ordinary game-thread policy calls.

**Configuration.** The first native implementation registered two example shapes
per algorithm. We then added a configuration asset that accepts the developer's
Schola definitions and supported actor/critic settings. Its editor command exports
`Config/ScholaRLTools.json`; UnrealBuildTool validates the request and generates
project-local C++ configurations during the next build. New shapes require
recompilation. Developers do not edit RLtools templates or a shared plugin
registry. Runtime settings remain distinct from compiled settings such as network
structure, batch size and replay/rollout capacity. Missing or stale configurations
produce actionable diagnostics instead of silently selecting another shape.

**Integration challenges.** PPO requires the original sampled action, probability
and value information. Editable public decision fields cannot establish that
provenance, so Schola retains the opaque native decision. Scaling a normalized
action into non-default game bounds and back can also change floating-point bits.
We reuse the original normalized action when the executed command is unchanged;
PPO still rejects additional gameplay modifications. Tests exercise both cases.
Session/generation identities prevent delayed callbacks and old decisions from
affecting a replaced learner even when its policy version restarts at zero.

**Verification and non-functional requirements.** UE **5.8.4**, Win64 Development
Editor, built the complete plugin and passed **40/40 selected automation tests**.
Coverage includes all algorithms, non-default bounds, experience attribution,
background publication, sharing, controls, snapshots and teardown with work in
flight. Nine configurations ran: six shipped profiles plus generated SAC 6/3,
TD3 7/4 and PPO 8/2 observation/action shapes. The build-generator suite passed
**43 checks** for validation, deterministic output and project isolation. These
results establish numerical/API integration; they do not establish useful NPC
adaptation, rendering or packaged-game performance.

Input and configuration validation, bounded decision/experience storage and
identity-checked asynchronous completion address reliability. Named shared groups
reject incompatible definitions and settings, and remain isolated by world.
Blueprint settings, readable errors, documented commands and keyboard-operated
slides improve access to the developer workflow. Execution budgets remain
cooperative: an individual numerical update cannot be preempted, and we make no
hard frame-time guarantee. See the [evidence and NFR record](adaptive-npc-evidence.md).

**Process.** Focused PRs separate lifecycle/build foundations, native learning,
the policy/world adapter and configuration generation. PRs #31, #35 and #36 have
visible teammate approvals. Our repeatable local scripts are the current
verification path. AMD has its own CI/CD checks; they are not integrated into this
repository yet, and we are working with the partner to establish that integration.
This is an outstanding process dependency, not a claim that these local results
ran in CI.

**Implementation:** [#30 foundations](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/30),
[#31 native learners](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/31),
[#35 policy and world service](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/35),
and [#36 configuration generation](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/36).
The [minimal test host](../../Examples/NativeLearning/README.md) exercises the plugin
without depending on the older examples' learner code.

**Shared policy Team**： We developed a benchmark prototype to compare five architectures (A–E) and evaluate their inference behaviour (PR #24). We also added macOS ARM64 build support for Schola (PR #32) and implemented Unreal NNE Automation Tests for architectures A, C, and D. All three passed and their outputs were validated against Python ONNX Runtime reference results (PR #33).

### Background NPCs — CampusCrowd

Built a 1.3 × 1.4 km campus demo using imported Toronto building geometry and up to 100,000 Mass students. It reuses Schola’s background inference API and the D2 ONNX model. Mass entities provide crowd simulation; background workers batch model calls while the game thread gathers observations and applies completed actions. Route following and local separation combine with model steering.

The main scaling issue was queue capacity. At 100,000 students, the default per-frame submission limit leaves a backlog. Higher queue and submission limits restore the requested decision rate, but do not restore 60 FPS. The HUD reports achieved throughput and highlights rates below 95% of target; timed captures also return exit status 3 when that threshold is missed. This prevents a smooth frame rate from hiding missed decisions.

Build, package, comparison, and capture scripts support repeatable local runs. Recorded CAD checks report five passing import tests, valid geometry, and routes clear of buildings. Implementation was merged in [PR #25](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/25). Review evidence should be checked on the PR before submission.

Evidence: [implementation and setup](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md), [CAD import tests](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/cad-import-tests.json), [geometry validation](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/geometry-validation.json), and [same-scene comparison](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/comparison.md).

Local automated tests and scripted builds/packages replace GitHub Actions for this Unreal workload: provisioning an engine image and native dependencies is unsuitable for the course’s short hosted jobs. This is the rubric’s documented local-process alternative, not deployment on merge. Five CAD regression tests and all 43 Unreal background/Mass tests passed on 5 November 2026. The editor build, packaging, and standalone smoke test also passed: 20,000 agents delivered 199,922 decisions/s at 59.98 FPS, with no dropped requests during capture. [Retained D3 validation](../../Examples/CampusCrowd/Evidence/d3-validation-2026-11-05/README.md). [Validation commands and NFR evidence](../../Examples/CampusCrowd/README.md#local-validation-and-deployment-d3) cover setup, tests, input bounds, overload errors, keyboard controls, and local packaging. The unused Android file server was disabled and its generated token removed from the current config.

## 3. Before vs. After

|Subteam| Workflow or quality | Before D3 | After D3 | Supporting evidence |
| --- | --- | --- | --- | --- |
|Adaptive NPCs| Reuse in another task | Example-owned learners and a SAC-specific integration prototype | Schola policy API operates SAC, TD3 and PPO from plugin modules | PRs #31, #35; policy tests |
|Adaptive NPCs| Observation/action configuration | Shapes embedded in prototype/native template code | Configuration asset exports and compiles project-specific shapes | PR #36; generated 6/3, 7/4 and 8/2 profiles |
|Adaptive NPCs| Ownership and lifecycle | Prototype-specific runtime management | World service, independent/default ownership, explicit sharing and asynchronous controls | Sharing, reset and teardown tests |
|Adaptive NPCs| Reproducible Unreal evidence | Separate prototype evidence; general backend unverified | UE 5.8.4 build and 40/40 selected tests, plus 43 generator checks | [Evidence](adaptive-npc-evidence.md) |
|Background NPCs| Integration and scale | 20,000-agent comparison scene | Campus geometry, routes, camera controls, and runs up to 100,000 agents | [Recorded performance](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance) |
|Shared Policy| Architecture comparison | No systematic comparison | Evaluated five architectures (A–E) and their inference trade-offs | PR #24 |
|Shared Policy| Unreal compatibility | Mainly Python testing | A, C and D passed Unreal NNE tests | PR #33 |
|Shared Policy| macOS support | Limited build support | Added ARM64 build support | PR #32 |

The adaptive comparison concerns capabilities and reproducibility, not a claimed
speedup or higher catch rate. D2 gameplay results remain evidence for those
prototypes. A newer published policy proves that updates reached inference; useful
adaptation requires a task-level comparison against a frozen policy under the
same conditions. The presentation includes a separate live-game demonstration.

### CampusCrowd measurements

**20,000 agents making 200,000 decisions per second at 60 FPS.** [Watch the CampusCrowd demo](https://utoronto-my.sharepoint.com/:v:/g/personal/maxh_xu_mail_utoronto_ca/IQDEpcI8CITpT6vs0vv20yCwAU4lH9gFIKJzbEZhq47R4p8?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=R3tV6g).

On a Ryzen 5 7600X and RX 7800 XT at 1600×900 with a 60 FPS cap, two CPU inference workers, batches of 64, and 10 decisions/s per student:

| Students | FPS | Completed decisions/s | Target decisions/s |
| ---: | ---: | ---: | ---: |
| 20,000 | 60 | 200,000 | 200,000 |
| 50,000 | 60 | 499,000 | 500,000 |
| 100,000, default limits | 36 | 895,000 | 1,000,000 |
| 100,000, raised limits | 39 | 997,000 | 1,000,000 |

These rounded results come from the [recorded README](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance). They describe local runs, not a controlled D2-to-D3 speedup. A separate same-scene 2,000-agent comparison reduced game-thread inference from 1.83 to 0.34 ms/frame; both modes reached the 60 FPS cap. Background inference added about one frame of action latency. The scene demonstrates a production use case; cross-platform validation and a trained policy remain open.

## 4. Reflection

### Adaptive NPCs

**What worked well.** Keeping task semantics in the game and numerical learning
inside the native learner gave us one policy workflow across three algorithms.
Testing the actual policy and world service exposed lifecycle and conversion
problems that standalone learner tests could not. Configuration generation also
removed an artificial restriction on developer-defined shapes while preserving
RLtools' compile-time requirements.

**What was harder.** Safe background learning involves more than moving Train to
a worker. Rewards may arrive after action selection expires; a full queue must
preserve a retryable decision; a reset must invalidate every affected shared
binding. PPO adds strict action/provenance requirements. We responded with bounded
retention, explicit admission outcomes and identity-checked operation completion.
The cost is a more explicit asynchronous lifecycle for game developers to handle.

**Path to D4.** First, connect the policy to the live-agent coordinator and
existing-environment adapter so observations, actions, rewards and episode
boundaries flow automatically. Next, evaluate learning versus frozen policies
across repeated seeds and behavior changes, measuring gameplay-thread latency and
memory at realistic NPC counts. Resumable checkpoint persistence and validation
in a separate packaged project remain distinct follow-ups. Captured snapshots
currently preserve inference state only. CPU feedforward networks, Box
observations and bounded continuous Box actions define the supported scope;
recurrent models, accelerated devices and other spaces need separate implementation
and evidence. This keeps D4 acceptance criteria concrete rather than presenting
the wider RLtools feature set as already supported.

**Shared Policy Team**： D3 helped us move beyond the initial prototype and verify that multiple Shared Policy architectures can run in Unreal. We also learned that supporting selective inference does not automatically guarantee better performance. Next, we plan to complete NPC head-routing integration, improve the benchmark, and evaluate the architectures under more realistic conditions.

### Background NPCs — Max Xu

Reusing the existing scheduler made it possible to focus on scene integration and scale. Queue sizing was harder than increasing the agent count: at 100,000 students, admission limits caused a backlog while inference workers still had spare capacity. Reporting FPS and completed decisions together exposed that problem. Max’s D4 priority is UX and reducing boilerplate: simplify setup and controls, and make the API easier to integrate through reusable configuration. Game-thread scheduling cost, validation on another machine, and a trained policy remain technical limitations to track.
