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

The host includes three project requests in `Config/ScholaRLTools.json`, exported
through the configuration asset API: SAC 6/3, TD3 7/4 and PPO 8/2. Building the
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
