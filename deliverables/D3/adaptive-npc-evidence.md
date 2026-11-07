# Adaptive NPCs: D3 workflow and evidence

The D3 improvement is a reusable Schola integration for native RLtools learning.
The game defines observations, actions, rewards and episode conditions; Schola
connects them to SAC, TD3 or PPO inside the Unreal process. This follows the same
task/library boundary as Schola's Python integrations with SB3 and RLlib.
A second part, [SAC chase adaptation](#sac-chase-adaptation), tests whether a
learning chaser adapts when its opponent changes.

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

## SAC chase adaptation

This part asks whether a learning NPC beats a frozen copy of itself after its
opponent changes habit. It runs in a standalone terminal simulator that calls
RLtools SAC directly. It does not use the Schola policy or Unreal.

**Demo video:** [chaser adapting to a new runner](https://drive.google.com/file/d/1J_AV8wG49tRTUB4X1vTzty05bYBZ5JJi/view?usp=sharing)

### Reproduce

macOS or Linux with clang++ or g++. From `Examples/AdaptiveTag`:

```sh
sh Tools/ChaseSim/Build-ChaseSim.sh
Saved/ChaseSim/chase_sim --script Evidence/SAC/d3/demo-reactivity.script
Saved/ChaseSim/chase_sim --seed 1 --script Evidence/SAC/d3/demo.script
sh Tools/ChaseSim/ci-smoke.sh Saved/ChaseSim/chase_sim
```

The first demo (about 40 s, no training) shows the runners reacting to scripted
chasers. The second (about 5 min) runs the full story: untrained, train on
`rightturn`, snapshot, switch to `corner`, recover. Both end at the `chase>`
prompt; try `watch`, `eval 100` or `quit`. The full 3-seed runs are
`Evidence/SAC/d3/adapt-final/seed{1,2,3}.script`, with their output in the
matching `.txt` and `.csv` files.

### Task and baselines

Chase v2 uses a 100-unit board, 200-step rounds and a catch distance of 2. The
three habit runners are deterministic and never exceed runner speed:

| Runner | Calm habit | When the chaser is close |
| --- | --- | --- |
| `rightturn` | Only turns right | Turns right toward "away" |
| `clockwise` | Slides clockwise along the walls | Swerves around the chaser |
| `corner` | Heads for the corner with the biggest lead (25-unit switch margin) | Flees within radius 60 |

Catch rates on 100 fixed starts:

| Runner | Greedy | Intercept |
| --- | ---: | ---: |
| `rightturn` | 0.64 | 0.71 |
| `clockwise` | 0.17 | 0.94 |
| `corner` | 0.03 | 0.98 |
| `square` | 0.62 | 1.00 |
| `circle` | 0.23 | 0.56 |

Greedy pursuit fails on the fleeing runners while the intercept chaser succeeds,
so the task is learnable but not trivial.

### Learning speed

A sweep of about 40 variants over 3 seeds (`Evidence/SAC/d3/sweeps/`) measured
catch rate after 20,000 moves. More gradient updates per move helped most: the
old default reached 0.34 and 8 updates per move reached 0.76. A smaller catch
bonus also helped; gamma, a lower learning rate, batch 256, replay size and
warm-up did not. Stacking every option did not beat the best single choices, and
differences under about 0.1 are noise. The chosen defaults are 8 updates per
move, a 64-wide network, the relative observation and a catch bonus of 10, at
about 170 moves/s on a laptop CPU. For comparison, 1 update per move runs at
about 1,250 moves/s and 16 updates at about 85.

### Adaptation test

Train on `rightturn`, snapshot the policy, switch to `corner` and keep learning.
The snapshot is a frozen, immutable policy evaluated on the same 100 starts.
These criteria were fixed before scoring and never loosened:

| Criterion | Seed 1 | Seed 2 | Seed 3 | Result |
| --- | ---: | ---: | ---: | --- |
| 1. ≥0.50 on `rightturn` before the switch | 0.96 | 0.90 | 0.98 | Pass |
| 2. ≤0.80 on `corner` right after the switch | 0.26 | 0.56 | 0.38 | Pass |
| 3. Back to ≥0.80 within 20,000 moves | 20,346 | 9,708 | 14,655 | Seed 1 misses by 346 |
| 4. Snapshot >0.20 below learner at 20,000 moves | 0.17 | 0.13 | 0.29 | Seeds 1–2 miss |
| End of run: learner vs. snapshot | 1.00 vs 0.26 | 0.99 vs 0.56 | 1.00 vs 0.38 | Learner wins |

Criterion 5 (all of the above on all 3 seeds) is therefore **not met**. Moves
are counted from the switch and measured at eval checkpoints every 5 episodes,
about 550–600 moves apart, so seed 1's miss is within one checkpoint. Recovery
is not yet stable: seed 2 reached 0.80 early but was at 0.69 near the
20,000-move mark. Every seed ends well above its frozen snapshot.

An earlier, easier `corner` runner passed all five criteria. After the runners
were fixed to flee properly, greedy pursuit fell to 0.03 on `corner`, and those
older runs no longer apply. They are archived in `Evidence/SAC/d3/old-runners/`
and are not reported as current results.

### Verification and limitations

- [PR #42](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/42) adds a
  GitHub Actions workflow with a 5-minute timeout. It builds `chase_sim` with g++
  and runs 7 smoke checks, including that the snapshot stays frozen while the
  learner publishes new versions.
- The new runner tests in `ChaseSessionTest.cpp` are written but have not been
  run in Unreal.
- The chaser observes the runner's position, so it can learn the runner's habit
  from that. Results apply to this direct RLtools setup, not to Schola.

## Process and partner CI/CD

AMD has its own CI/CD checks. Those checks are not integrated into this codebase
yet; the team is working with the partner to establish that setup. Until then,
the two documented local commands above provide repeatable verification, with
machine-readable results and teammate-reviewed PRs. Local execution is reported
as local execution. The partner dependency and lack of automatic Unreal PR/merge checks
remain visible process limitations. The chase simulator is the exception: its
GitHub Actions workflow builds and tests it on PRs to `main` (see above).

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

For the chase adaptation, show the [demo video](https://drive.google.com/file/d/1J_AV8wG49tRTUB4X1vTzty05bYBZ5JJi/view?usp=sharing)
or run `demo.script` live (about 5 min).
