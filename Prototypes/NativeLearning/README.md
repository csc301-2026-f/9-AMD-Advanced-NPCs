# Native RLtools learning checks

This standalone target compiles the plugin's own learner implementation. It supplies
observations, actions and rewards without an Unreal simulation or example-owned code.

On Windows with Visual Studio C++ tools and CMake, run from the repository root:

```powershell
./Prototypes/NativeLearning/Run.ps1
```

Checks cover SAC, TD3 and PPO for every registered configuration: real experience
storage and actor changes, TD3 policy delay, PPO rollouts/epochs, frozen snapshots,
pause/reset/stop, invalid settings, duplicate outcomes, learner isolation, queue
overflow and concurrent inference/admission/training. Assertions remain active
in Release builds. C++20, precise floating point, disabled RTTI and disabled C++
exceptions match the native module requirements.

Run Unreal checks separately in the UE 5.8 AdaptiveSAC host:

```powershell
./Examples/AdaptiveSAC/Test-AdaptiveSAC.ps1 -Filter 'Schola.RLTools+Schola.Inference.Adaptive+ScholaOnlineLearning.SAC.Policy'
```

These checks establish numerical integration, not packaged gameplay or improvement
in a particular NPC task.
