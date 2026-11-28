# Native Schola learning host

This UE 5.8.4 host loads the complete Schola plugin and runs the actual native
SAC, TD3 and PPO policy/world tests. It has no dependency on the AdaptiveTag or
AdaptiveSAC example learner implementations.

From the repository root:

```powershell
./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
```

The script builds Development Editor, runs headless automation, checks every
matching source registration has a successful result, and saves logs and JSON
under `Saved/NativeLearning`. Build Accelerator detouring is disabled by default; use
`-UseBuildAccelerator` if desired.

The host includes five project requests in `Config/ScholaRLTools.json`: SAC 6/3,
TD3 7/4 and PPO 8/2, exported through the configuration asset API, the SAC 10/2
`chase-sac` profile used by the chase adaptation test, and the SAC 10/2
`chase-demo-sac` profile used by the chase demo below. Building the
project generates their configurations without modifying plugin templates.
See [configuration generation](../../Docs/Sphinx/guides/rltools_configuration_generation.rst)
for the editor workflow.

The `Schola.RLTools.Policy.SAC`, `.TD3` and `.PPO` tests demonstrate all shipped
and project-generated configurations:

1. Configure `URLToolsNPCPolicy` with an algorithm, profile and runtime settings.
2. Initialize with an `FInteractionDefinition`; wait for Ready and inspect the
   initialization operation.
3. Request and retrieve a decision, execute its returned command, and construct
   an attributed `FAdaptiveExperience`.
4. Submit the outcome and observe background storage, optimizer updates and a
   newer published policy version.
5. Evaluate a retained snapshot after training and binding shutdown.

Other tests cover sharing, controls, retries, PPO conversion, reset and teardown.
These are numerical/API tests with `-nullrhi`, not rendering or packaged gameplay
validation. Collection of observations/rewards and the gameplay loop remain the
calling integration's responsibility. The complete developer guide is
[Native adaptive policies](../../Docs/Sphinx/guides/native_adaptive_policy.rst).

## Chase adaptation through the plugin

`NativeLearning.ChaseAdaptation` runs the D3 chase adaptation test through the
shipped Unreal path: `UAdaptiveStepper` drives a chase agent (the `chase_sim`
world from `Examples/AdaptiveTag`, compiled into this host) with a
`URLToolsNPCPolicy` on the `chase-sac` project profile (10 observations,
2 actions, 64-wide networks, replay 10,000). It trains 150 episodes on
`rightturn`, captures a snapshot, switches to `corner`, and checks the four D3
criteria against chase_sim's 100 fixed evaluation starts. It takes several
minutes per seed, so the default filter leaves it out:

```powershell
foreach ($seed in 1, 2, 3) {
    ./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8' -Filter 'NativeLearning.ChaseAdaptation' -ChaseSeed $seed
}
```

Criterion 5 passes only if all three seeds pass. The engine-free twin of this
test, with recorded results, is `Tools/ScholaRLToolsNative/ChaseAdaptation.cpp`.

## Chase demo

A playable view of the same native learner. A chaser (red) learns to catch a runner
(green) while the game runs. It uses `URLToolsNPCPolicy` with the `chase-demo-sac`
profile, and the runner's six habits are ported from the terminal chase simulator in
`Examples/AdaptiveTag`.

To run it:

1. Run `Prepare-NativeLearning.ps1`, then build `NativeLearningEditor` (the test script above does both).
2. Open `NativeLearning.uproject` in UE 5.8.4 and press **Play**. No map setup is needed:
   `Config/DefaultEngine.ini` sets `ChaseDemoGameMode` as the default game mode.

| Key | Action |
| --- | --- |
| Space | Pause / resume |
| F | Steps per frame: 4, 12, 1 |
| 1–6 | Runner habit: straight, square, circle, right turn, clockwise, corner |
| Tab | Drive the runner yourself with WASD, or hand it back to the habit |
| L | Freeze / resume learning (the chaser keeps acting either way) |

The HUD shows episodes, the catch rate over the last 20 rounds, gradient updates,
learning state and who controls the runner. The rolling catch rate resets whenever
the habit, runner control or learning state changes, so you can watch it drop and
recover after a switch.

At the start, the chaser heads straight at the runner instead of using the untrained
network, which moves erratically. Between 2,000 and 12,000 training updates its moves
blend from that heading to the network's actions; the HUD shows this as
`LEARNING: WARM-UP n%`. SAC learns off-policy, so the warm-up moves still train it.
After that every move comes from the network.

The demo uses 8 training updates per move, like the D3 experiment. Warm-up moves get a
small random wobble (`WarmupNoise`) so the learner sees varied actions. At high F speeds the game makes moves faster than
the learner can train on; its queue is capped (1,024) so surplus moves are skipped and
it always trains on recent ones. F speeds up the game, not the learning.

Tunable values are properties on `ChaseDemoGameMode` (`Source/NativeLearningHost/ChaseDemo/`):
speeds, catch distance and bonus, the rolling window size, `CatchFlashDuration`
(how long the catch flash holds), `ActorDiameter` (circle size) and `ArenaScreenFraction`
(how much of the screen the arena fills), `GradientStepsPerMove`, `WarmupStartUpdates` /
`WarmupEndUpdates` (the handover).

Source: `ChaseDemoGameMode` (loop, input, rewards), `ChaseDemoRunnerPatterns` (runner
habits), `ChaseDemoActor` (visuals), `ChaseDemoHUD`.

## Verified locally

On 2026-11-05, UE 5.8.4 Win64 Development Editor built this host with every
Schola module enabled and the root plugin descriptor unchanged. All **40**
selected automation tests passed: four configuration-generation tests, 16
policy/world integration tests, two native learner tests and 18 common adaptive
API tests. Nine compiled configurations were exercised (six shipped and three
project-generated). The script verified no matching
source registrations were missing.

The standalone native learner suite also passed for all six compiled
algorithm/configuration combinations. The build-generator tests cover validation,
determinism and project isolation using the same C# code as UnrealBuildTool.
A focused strict Sphinx build of the four linked native guides and whitespace
checks passed.

The full build reports existing MSVC C4305 float-conversion warnings in unchanged
RLlib/SB3 settings headers. This validation does not establish rendering,
packaged gameplay, checkpoint persistence or useful NPC adaptation.
