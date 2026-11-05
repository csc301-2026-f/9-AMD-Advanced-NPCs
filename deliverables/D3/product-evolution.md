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

## 3. Before vs. After

|Subteam| Workflow or quality | Before D3 | After D3 | Supporting evidence |
| --- | --- | --- | --- | --- |
|Adaptive NPCs| Reuse in another task | Example-owned learners and a SAC-specific integration prototype | Schola policy API operates SAC, TD3 and PPO from plugin modules | PRs #31, #35; policy tests |
|Adaptive NPCs| Observation/action configuration | Shapes embedded in prototype/native template code | Configuration asset exports and compiles project-specific shapes | PR #36; generated 6/3, 7/4 and 8/2 profiles |
|Adaptive NPCs| Ownership and lifecycle | Prototype-specific runtime management | World service, independent/default ownership, explicit sharing and asynchronous controls | Sharing, reset and teardown tests |
|Adaptive NPCs| Reproducible Unreal evidence | Separate prototype evidence; general backend unverified | UE 5.8.4 build and 40/40 selected tests, plus 43 generator checks | [Evidence](adaptive-npc-evidence.md) |
|Shared Policy| Architecture comparison | No systematic comparison | Evaluated five architectures (A–E) and their inference trade-offs | PR #24 |
|Shared Policy| Unreal compatibility | Mainly Python testing | A, C and D passed Unreal NNE tests | PR #33 |
|Shared Policy| macOS support | Limited build support | Added ARM64 build support | PR #32 |

The adaptive comparison concerns capabilities and reproducibility, not a claimed
speedup or higher catch rate. D2 gameplay results remain evidence for those
prototypes. A newer published policy proves that updates reached inference; useful
adaptation requires a task-level comparison against a frozen policy under the
same conditions. The presentation includes a separate live-game demonstration.

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
