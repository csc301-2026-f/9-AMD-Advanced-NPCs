# Adaptive NPCs: D3 workflow and evidence

The D3 improvement is a reusable Schola integration for native RLtools learning.
The game defines observations, actions, rewards and episode conditions; Schola
connects them to SAC, TD3 or PPO inside the Unreal process. This follows the same
task/library boundary as Schola's Python integrations with SB3 and RLlib.

## Reproduce the developer workflow

Use Windows, Unreal Engine **5.8.4**, Visual Studio 2022 with Game development
with C++, and a Windows SDK. No Python service or pre-trained model is required.
From the repository root, run:

```powershell
./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
./Test/rltools_generation/Run.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
```

The first command builds the complete plugin in a minimal host, then runs the
actual policy/world-service tests headlessly. First-time C++ compilation can take
several minutes; subsequent runs reuse the build. The script fails if an expected
test is missing or unsuccessful. Results appear under
`Examples/NativeLearning/Saved/NativeLearning/`, including `automation-summary.json`.
The second command runs the same C# configuration generator used by UnrealBuildTool.

The SAC, TD3 and PPO policy tests each demonstrate configuration, initialization,
Ready status, decision requests, execution of the returned command, attributed
outcomes, background training and a newer published policy version. They also
exercise retained snapshots. See the [host instructions](../../Examples/NativeLearning/README.md),
[policy guide](../../Docs/Sphinx/guides/native_adaptive_policy.rst) and
[configuration guide](../../Docs/Sphinx/guides/rltools_configuration_generation.rst).

For a different task, create an RLtools Configuration Asset, supply its Schola
observation/action definitions and supported network settings, and select
**Generate Project Configuration**. Rebuild the project and assign the asset to
the native policy. The project owns `Config/ScholaRLTools.json`; generated C++
lives under `Intermediate/ScholaRLTools`. New shapes require recompilation.

## Verified scope

Local validation on 5 November 2026 used UE **5.8.4**, Win64 Development Editor.

| Check | Result | What it establishes |
| --- | --- | --- |
| Unreal automation | **40/40 passed** | Four generation, 16 policy/world, two native learner and 18 common API tests |
| Compiled profiles exercised | **9** | Six shipped profiles plus generated SAC 6/3, TD3 7/4 and PPO 8/2 observation/action shapes |
| Build-generator checks | **43 passed** | Input validation, deterministic output, resource limits and project isolation |

The machine-readable [Unreal results](evidence/adaptive-unreal-summary.json)
list each completed test. The [validation record](evidence/adaptive-validation.json)
identifies the integrated source revision and generator result. PRs
[#31](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/31),
[#35](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/35) and
[#36](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/36) provide the
implementation, peer approvals and original verification details.

## Non-functional requirements

| Concern | Implemented behavior | Evidence |
| --- | --- | --- |
| Responsiveness | Construction, reset, incremental training and disposal run on workers; bounded world scheduling avoids one thread per NPC | `BoundedFairScheduling`, `DelayedCreationCannotReviveBinding` |
| Data integrity | Native decision provenance, session/generation identity and original observations reject altered or stale attribution | SAC/TD3/PPO policy tests; `DecisionRetirementAndLateRewards` |
| Safe resource use | Bounded queues and decision retention; queue-full and temporarily unavailable admission preserve eligible retries | `QueueFullRetry`, `PPOTemporarilyUnavailableRetention` |
| Isolation | Independent learners by default; explicit sharing checks full compatibility and stays within one world | `IndependentAndSharedOwnership`, `SharedCompatibility` |
| Lifecycle reliability | Shared release/reset and world teardown cannot revive obsolete bindings; snapshots remain independently evaluable | `SharedResetWithReleaseInFlight`, `WorldTeardownWithWorkInFlight`, `ControlsResetAndSnapshot` |
| Developer access | Blueprint/C++ settings, observable operation completion, useful validation errors and documented commands | `ConfigurationAndBlueprintAPI`, generation asset validation tests |

Scheduling limits are cooperative. Individual numerical updates cannot be
preempted; no fixed frame-time or production-scale throughput claim follows from
these tests. Validation used `-nullrhi`: rendering, packaged gameplay and useful
NPC adaptation require separate evidence. Existing C4305 conversions in unchanged
RLlib/SB3 headers remain build warnings. The backend supports CPU feedforward
networks, Box observations and bounded continuous Box actions. Snapshots preserve
inference state; resumable training persistence remains follow-up work.

## Process and partner CI/CD

AMD has its own CI/CD checks. Those checks are not integrated into this codebase
yet; the team is working with the partner to establish that setup. Until then,
the two documented local commands above provide repeatable verification, with
machine-readable results and teammate-reviewed PRs. Local execution is reported
as local execution. The partner dependency and lack of automatic PR/merge checks
remain visible process limitations.

## Presentation demo

The [live-demo slide](index.html#demo) reserves the transition to the team's game.
Show the task, the NPC's decisions and scored outcomes, then the learning state
or changed behavior. Name which implementation the game uses. A legacy prototype
demonstrates its own gameplay behavior; the new integration's tests demonstrate
the reusable policy API. Do not combine the two into an unsupported claim.

If the game is unavailable during the talk, use the already-built NativeLearning
host to show the policy tests and their results. This fallback demonstrates the
API workflow, not rendered gameplay. Rehearse the actual game launch separately;
no unverified recording, launch command or key binding is supplied here.
