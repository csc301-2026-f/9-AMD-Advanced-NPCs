# AdaptiveSAC test host

Runs RLtools SAC (`OnlineSACRuntime`, the single copy in `../AdaptiveTag/Source`) behind Schola's
`UAdaptiveNPCPolicy`, with the Schola plugin enabled. No Python anywhere.

```powershell
./Test-AdaptiveSAC.ps1                       # builds, then runs ScholaOnlineLearning.SAC.Policy.*
./Test-AdaptiveSAC.ps1 -Filter Schola.Inference.Adaptive   # the plugin's own adaptive tests in this host
```

`Prepare-AdaptiveSAC.ps1` (run by the test script) generates the git-ignored `Plugins/Schola`:
a copy of the root `Schola.uplugin` minus `ScholaMass`, plus junctions to the root `Config`,
`Resources` and `Source/<module>` folders. Shared plugin files are never modified.
- `AdditionalPluginDirectories: ["../.."]` does not work: UBT only looks for `.uplugin` files in
  *child* directories of a plugin folder.
- `ScholaMass` is excluded because its Build.cs depends on `MassCore`, which UE 5.7 does not have
  (MassGameplay has `MassCommon`). That is a shared-plugin fix for the team.

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
