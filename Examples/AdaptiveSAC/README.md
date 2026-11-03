# AdaptiveSAC test host

Runs RLtools SAC (`OnlineSACRuntime`, the single copy in `../AdaptiveTag/Source`) behind Schola's
`UAdaptiveNPCPolicy`, with the Schola plugin enabled. No Python anywhere.

## Engine support for D3

This host currently targets **Unreal Engine 5.8 on Windows**, including `ScholaMass`.
Both `NNERuntimeORT` and `NNERuntimeBasicCpu` are enabled for the background backend tests.
UE 5.7 and cross-version compatibility are deferred until later integration work.
This is a host-specific requirement; it does not change the engine version of the original Adaptive Tag demo
or the support policy for Schola's other examples.

Install UE 5.8, Visual Studio 2022 with the Game development with C++ workload, and a Windows SDK.
Run the following from `Examples/AdaptiveSAC`:

```powershell
./Test-AdaptiveSAC.ps1                       # builds, then runs ScholaOnlineLearning.SAC.Policy.*
./Test-AdaptiveSAC.ps1 -Filter Schola.Inference.Adaptive   # the plugin's own adaptive tests in this host
```

The script discovers UE 5.8 through the registry or Epic Launcher. For a custom installation, pass
`-EnginePath 'D:/Apps/Epic Games/UE_5.8'` or set `UE_ENGINE_ROOT` to the engine root.
The selected installation's `Engine/Build/Build.version` must report version 5.8; another engine version
is rejected before generating the host plugin or starting a build.

For the D3 integration check, include the native policy, adaptive base, background scheduler/adapter,
Mass bridge, existing paradigm and shared-policy stepper tests:

```powershell
./Test-AdaptiveSAC.ps1 -Filter 'ScholaOnlineLearning.SAC.Policy+Schola.Inference.Adaptive+Schola.Inference.Background+Schola.Mass+Schola.Inference.Paradigms+Schola.Inference.SharedPolicy.Stepper'
```

The full automation log is written to `Saved/OnlineSAC/UnrealAutomation.log`.
The Windows script does not establish Linux support; the UE 5.8/Linux build and background/Mass checks
must be verified in that environment separately.

### Recorded validation

On Windows with UE **5.8.4**, the complete `AdaptiveSACEditor` host built successfully and all **73**
tests in the integration command above passed: 36 background, 7 Mass, 10 adaptive lifecycle,
5 native SAC policy, 6 paradigm and 9 shared-policy stepper tests.
UE 5.7 compatibility, Linux validation and packaged gameplay are not established by this run.

## Full plugin setup

`Prepare-AdaptiveSAC.ps1` (run by the test script) generates the git-ignored `Plugins/Schola`:
an unchanged copy of the root `Schola.uplugin`, plus junctions to the root `Config`,
`Resources` and every `Source/<module>` folder. All modules, including `ScholaMass`, are included.
Shared plugin files are never modified. Re-running the script upgrades a previously generated host
that excluded `ScholaMass` by adding its source junction and restoring the full descriptor.
- `AdditionalPluginDirectories: ["../.."]` does not work: UBT only looks for `.uplugin` files in
  *child* directories of a plugin folder.
- The old UE 5.7 module-exclusion workaround is removed. This host uses the existing UE 5.8 Mass dependencies.

## UOnlineSACPolicy (`Source/OnlineSACSchola`)
- `Init`: refuses anything but a 6-value Box observation and a 2-value Box action (`GetLearnerError`),
  calls the base `Init`, creates the learner and starts a local session (`StartSession`, no handshake).
- `DispatchDecision`: `learner.Act`, maps [-1, 1] to the action bounds, and calls `CompleteDecision`
  synchronously with the snapshot version (allowed by the base contract).
- `SubmitExperience(FAdaptiveExperience)`: our own entry point (no team-wide one is agreed yet);
  maps `ExecutedAction` back to [-1, 1] and calls `AddExperience`.
- `ShutdownLearner()` and `BeginDestroy` stop the learner thread. The base `Shutdown()` is not virtual,
  so calling it directly stops the session but leaves the (idle) learner thread until the next
  `SubmitExperience`, `ShutdownLearner` or destruction.
