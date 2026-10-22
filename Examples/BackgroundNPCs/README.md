# Background NPC comparison

This project runs the same Mass crowd twice: once **without** Schola's background NPC API, and once **with** it. Only the inference path changes. The scene, agent count, ONNX policy, observations, decision rate, and movement code are identical.

| Mode | How each decision is made |
| --- | --- |
| **Without API** (`-Mode=Sync`) | The straightforward approach: every due agent writes its observation and calls `RunSync` on a batch-1 ORT model instance on the game thread. The frame waits for every call. |
| **With API** (`-Mode=Background`) | `ScholaMass` gather/apply processors submit due agents' rows to a shared bucket. The scheduler batches rows (up to 64) onto two background workers. Completed actions are applied on a later frame, so the game thread never waits on the model. |

The HUD shows frame rate, frame time (mean and p99), game-thread inference time per frame, decisions per second against the target, agents per model call, observation-to-action latency, and dropped requests. Press **Tab** to switch modes live.

## Policy

`Content/Models/crowd_policy.onnx` is a hand-built steering network, **not a trained policy**. Its action is the direction to the agent's goal plus a bounded "wander" term from a randomly initialized 64→256→256→2 MLP. The MLP gives every decision a realistic small-network inference cost, and its output visibly steers agents. `Tools/make_crowd_policy.py` regenerates it (requires `numpy` and `onnx`). The demo measures scheduling and frame cost, not navigation quality.

## Build and run

Tested on Linux with Unreal Engine 5.8.3 (Vulkan). The Windows path has not been tested.

```bash
export UE_ENGINE_ROOT=/path/to/UnrealEngine-5.8.3
cd Examples/BackgroundNPCs
Scripts/build.sh        # editor target; links Plugins/Schola to this repository's root
Scripts/run.sh          # starts in Background mode; press Tab to switch
Scripts/run.sh -Mode=Sync -Agents=5000
Scripts/package.sh      # standalone game in Saved/Package/Linux
```

`Config/Linux/LinuxEngine.ini` disables PSO precaching (`r.PSOPrecaching=0`) because the Mesa RADV driver crashed while precompiling compute pipelines in packaged Vulkan builds. The setting applies to both modes equally.

On Windows, create the plugin link once with `New-Item -ItemType Junction -Path Plugins/Schola -Target ../../..` from this directory, then open `BackgroundNPCs.uproject` or build the `BackgroundNPCsEditor` target.

| Argument | Default | Meaning |
| --- | --- | --- |
| `-Mode=Sync\|Background` | `Background` | Inference path at startup |
| `-Agents=N` | `20000` | Number of Mass entities |
| `-DecisionHz=F` | `10` | Decisions per agent per second |
| `-MaxFPS=F` | `60` | Frame cap (`0` for uncapped) |
| `-Workers=N`, `-MaxBatch=N` | `2`, `64` | Background scheduler settings |
| `-CaptureSeconds=S -WarmupSeconds=S -Report=file.json` | off | Measure, write a JSON report, and exit |

## Reproduce the comparison

After `Scripts/package.sh`:

```bash
Scripts/compare.sh    # 10 s warm-up + 30 s capture per mode; writes Saved/Comparison/summary.md
Scripts/record.sh     # X11 only: records both modes and writes Saved/Recording/side-by-side.mp4
```

The two modes run one after another in separate processes, so they don't compete for the CPU. `record.sh` uses `ffmpeg` and `xdotool`.

## Files

| Path | Contents |
| --- | --- |
| `Source/BackgroundNPCs/CrowdComparisonHost.*` | Scene setup, Mass entity creation, mode switching, statistics, and JSON reports |
| `Source/BackgroundNPCs/CrowdProcessors.*` | Shared observation/movement code; the ScholaMass gather/apply processors; the per-agent synchronous baseline |
| `Source/BackgroundNPCs/CrowdGameMode.*` | Game mode and HUD |
| `Content/Models/crowd_policy.onnx` | Policy loaded from disk at runtime (staged as a non-UFS file) |
| `Tools/` | Model generator and report summarizer |
| `Scripts/` | Linux build, run, package, measurement, and recording scripts |
