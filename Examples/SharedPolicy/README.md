# Shared Policy example

The D4 example uses Schola's Architecture C policy to steer six NPCs with
two model calls per decision step. Three blue cubes follow the beacon in
single file, fastest closest to the beacon, and three orange cubes move away.
The follow spots turn with the beacon's direction of travel. The blue cubes have
maximum speeds of 30%, 60%, and 100% of the beacon's speed, respectively.
They share the same steering model; the demo applies each cube's speed
when turning its steering output into movement.

## Build on macOS

Use Unreal Engine 5.8.4 and the dependencies in the
[macOS setup guide](../../Docs/macOS-Setup.md). From the repository root:

```bash
export UE_ENGINE_ROOT="/Users/Shared/Epic Games/UE_5.8"
bash Examples/SharedPolicy/Scripts/prepare.sh
"$UE_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" \
  SharedPolicyEditor Mac Development \
  -Project="$PWD/Examples/SharedPolicy/SharedPolicy.uproject" \
  -NoHotReloadFromIDE -NoUBA
```

## Play and check the log

Open `SharedPolicy.uproject`. The editor opens `LV_SharedPolicyD4` by default.
Press **Play**, click the viewport, and move the beacon with **WASD**.
Filter **Output Log** for `LogSharedPolicyD4` to see six applied actions
and two model calls, one per active role. The closer/farther counts can
drop at the arena boundary or when a follower reaches its spot.

The two embedded ONNX models use deterministic, untrained steering rules.
The Toward model returns its input direction and the Away model negates it.
The [model generator](Tools/generate_shared_policy_demo_models.py) reproduces
the embedded header using Python and the `onnx` package. The demo map
identifies the beacon by the editor label or Actor Tag `D4_Target`.

Run the headless smoke test from the repository root:

```bash
"$UE_ENGINE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/Examples/SharedPolicy/SharedPolicy.uproject" \
  /Game/LV_SharedPolicyD4 -game -nullrhi -unattended -nosound -NoSplash \
  -SharedPolicyD4Smoke -stdout
```

Check for `D4 smoke: PASS` in the log. The check relocates the beacon and
verifies the model-call count, steering, and separation of the followers.
