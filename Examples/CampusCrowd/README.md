# North Campus crowd demo

**20,000 agents making 200,000 decisions per second at 60 FPS.** [Watch the CampusCrowd demo](https://utoronto-my.sharepoint.com/:v:/g/personal/maxh_xu_mail_utoronto_ca/IQDEpcI8CITpT6vs0vv20yCwAU4lH9gFIKJzbEZhq47R4p8?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=R3tV6g).

Up to 100,000 Mass students walk a 1.3 × 1.4 km campus. Each student asks a neural network for a steering decision 10 times per second. Schola batches those requests and runs them on background worker threads.

The campus geometry is based on the City of Toronto's [3D massing data](https://opendata.toronto.ca/city.planning/3d-massing/2025/AutoCAD/50H_NORTH.dwg). It is stored with Git LFS, so run `git lfs pull` after cloning.

```bash
cd Examples/CampusCrowd
export UE_ENGINE_ROOT=/path/to/UnrealEngine-5.8
Scripts/build.sh
Scripts/run.sh -Agents=50000
```

## Performance

AMD Ryzen 5 7600X and Radeon RX 7800 XT, 1600×900, 60 FPS cap. Each student makes 10 decisions/s. Two inference workers handle batches of up to 64 rows.

| Students | FPS | Decisions/s (of target) | Game-thread inference | Crowd movement | Worker model time | Observation → action |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2,000 | 60 | 20,000 (100%) | 0.3 ms | 1.1 ms | 36 ms/s | 17 ms |
| 20,000 | 60 | 200,000 (100%) | 1.9 ms | 1.7 ms | 341 ms/s | 16 ms |
| 50,000 | 60 | 499,000 (100%) | 4.4 ms | 2.8 ms | 761 ms/s | 15 ms |
| 100,000 | 36 | 895,000 (90%) | 15.8 ms | 5.1 ms | 1,328 ms/s | 45 ms |

Game-thread times are per frame. Game-thread inference covers ScholaMass's apply and gather steps, including the demo's own code that writes observations and applies actions (about 2 ms of the 15.8 ms at 100,000). Worker model time is summed across both workers, out of 2,000 ms/s available, and comes to 1.5–1.8 µs per decision at every crowd size. Observation → action is about one frame.

Without the 60 FPS cap, 20,000 students run at about 216 FPS and 50,000 at about 93.

At 100,000 students the game thread limits the frame rate, while the workers are only about two-thirds busy. The 90% shortfall comes from the per-frame row cap:

- At 36 FPS the crowd needs about 27,800 decisions per frame, but the default cap admits 25,000 (half of the 50,000-row queue).
- Students over the cap stay due, so every frame scans and sorts a backlog of about 77,000 students. That slows the frame further.
- With `-MaxPendingRows=100000 -MaxRowsPerFrame=50000`, the run delivers 100% of the target (about 997,000/s) at 39 FPS.

Even without the game-thread cost, 100,000 students would not reach 60 FPS, because the GPU takes about 18 ms per frame. Latency rises to 45 ms because one frame's batches take the workers about 20 ms, so some finish after the next apply step and wait another frame.

The HUD shows the same measurements. Below 95% of target, the decision rate turns red and the HUD names the cause.

| Option | Default | Effect |
| --- | --- | --- |
| `-Agents` | 2000 | Crowd size, 1–100,000 |
| `-DecisionHz` | 10 | Decisions per student per second |
| `-Workers` | 2 | Inference worker threads |
| `-MaxBatch` | 64 | Rows per model call |
| `-MaxPendingRows` | ~50 ms of target decisions (≥ 4,096) | Inference queue limit; results older than 100 ms are discarded |
| `-MaxRowsPerFrame` | half the queue | Rows submitted per frame |
| `-Mode` | Background | `Sync` runs one model call per student on the game thread |

To measure, add `-WarmupSeconds=10 -CaptureSeconds=10 -Report=out.json`. The run exits after writing the JSON report. Exit status 3 means the run delivered less than 95% of the target decision rate.

## Neural network

`Content/Models/crowd_policy.onnx` (328 KB, ONNX opset 17) runs on ONNX Runtime CPU (`NNERuntimeORTCpu`). It is the same network as in [BackgroundNPCs](../BackgroundNPCs/README.md), and untrained.

| | |
| --- | --- |
| Architecture | MLP: 64 → 256 → 256 → 2 (ReLU, ReLU, tanh) |
| Parameters | about 83,000; about 82,000 multiply-adds per decision |
| Input (64 floats) | Direction to next route node (2), distance to it (1), velocity (2), 59 sinusoidal position features |
| Output (2 floats) | Steering = goal direction + 0.6 × tanh(MLP) |
| Weights | Random He initialization, seed 301 (`../BackgroundNPCs/Tools/make_crowd_policy.py`) |

The output gets 15% weight in each student's heading. Route following and separation from nearby students provide the rest.

## Setup and controls

This is a local Unreal application, with no hosted URL. The video above lets a tester evaluate the demo without installing Unreal. Building requires Linux, Unreal Engine 5.8 (measured on 5.8.3), its C++ toolchain, Git LFS, and Schola’s native dependencies. Follow the [Schola installation guide](../../README-Schola.md) for dependencies. The build script links the repository’s plugin into the example. First-time engine and dependency setup can take longer than a few minutes.

Run `git lfs pull` from the clone, set `UE_ENGINE_ROOT` to your installed engine, then use the build and run commands above. For the recorded workload, run:

```bash
Scripts/run.sh -Agents=20000 -DecisionHz=10 -Workers=2 -MaxBatch=64
```

Click the game viewport to focus keyboard input. Use **1–3** for camera views, **WASD + Q/E** to fly, right mouse to look, **Tab** to compare inference modes, **N** to show routes, and **Space** to pause. Watch the HUD’s FPS, game-thread inference time, completed decisions/s versus target, and action latency. Expected background workload: 20,000 agents requesting 200,000 decisions/s; the recording reaches 60 FPS on the documented hardware.

Configuration lives in [Config](Config), the option table above, and `UE_ENGINE_ROOT`; the path shown in setup is a placeholder. No service credentials or user accounts are needed to run the demo. The model and generated campus are versioned; the original DWG is needed only to rebuild or verify the CAD import.

## Local validation and deployment (D3)

We do not use GitHub Actions for the Unreal build and test workload. Standard hosted runners do not include our configured Unreal Engine installation or the graphics environment needed for the visual demo. Provisioning an Unreal image, native dependencies, and cooked assets is unsuitable for this project’s short Actions jobs and shared minutes budget. We use local automated tests, build verification, and packaging instead, under the assignment’s project-constraints exception. These checks are manually invoked; merges to `main` do not deploy automatically.

From this directory, install the CAD test dependencies in a virtual environment and run five regression tests:

```bash
python3 -m venv /tmp/campuscrowd-tests
/tmp/campuscrowd-tests/bin/python -m pip install ezdxf==1.4.5 shapely==2.2.0
/tmp/campuscrowd-tests/bin/python -m unittest discover -s Tools/tests -v
```

The tests cover typed and legacy CAD transforms, concave roofs, window holes, and a University College source region. All five passed locally on 5 November 2026 using the pinned dependencies. [Audit test result](Evidence/local-validation-2026-11-05.txt). They do not require Unreal or the original DWG. [Recorded CAD evidence](Evidence/cad-import-tests.json) also includes the source-region decode count.

For an installed engine, run build verification and the background/Mass Unreal suites:

```bash
Scripts/build.sh
"$UE_ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CampusCrowd.uproject" \
  -nullrhi -unattended -nosound \
  "-ExecCmds=Automation RunTests Schola.Inference.Background+Schola.Mass;Quit" \
  "-ReportExportPath=$PWD/Saved/D3Tests"
Scripts/compare.sh Saved/D3Comparison -Agents=20000 -DecisionHz=10 -Workers=2 -MaxBatch=64
Scripts/package.sh
Scripts/run.sh -Agents=20000 -DecisionHz=10
```

The final command selects the package when it exists. Confirm that the campus loads, movement and camera controls work, and the HUD reports decision throughput. Keep the test report, comparison JSON, build/package logs, engine version, hardware, and commit ID with the release evidence. Comparison exit status 3 means a mode missed 95% of its decision target; inspect the report rather than treating this as a crash. The headless tests do not validate rendering. On 5 November 2026, the D3 editor build, all 43 background/Mass tests, packaging, and standalone smoke test passed on UE 5.8.3/Linux. The packaged 20,000-agent run reached **59.98 FPS and 199,922 decisions/s**, with zero dropped requests during the timed capture. Camera switching, inference-mode switching, pause/resume, and normal close were checked separately. [Retained D3 results, logs, and screenshots](Evidence/d3-validation-2026-11-05/README.md).

Optional full CAD validation: place the original `50H_NORTH.dwg` at the repository root, install [Tools/requirements.txt](Tools/requirements.txt), then run `python Tools/validate_campus.py`. It checks the source hash, finite vertices, triangle indices, connected reciprocal routes, and three-metre building clearance. [Recorded geometry validation](Evidence/geometry-validation.json) is available without that source file.

Before merging, attach local results to the PR and request a teammate’s review. After merging, package the selected commit and smoke-test it locally. [Merged PR #25](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/25) establishes implementation history; a merge alone does not establish peer approval.

## Non-functional requirements

- **Performance:** the HUD and JSON reports expose FPS, inference cost, throughput, and action latency. Queue limits bound pending work, and old actions are discarded. See the measurements above for the 100,000-agent limit.
- **Input validation and errors:** crowd size, decision rate, worker count, and camera preset are clamped. Submissions are clamped to queue capacity with a warning. Missing route data or model failures produce an explicit error. Missed decision targets are reported in text as well as color.
- **Usability:** keyboard camera presets, pause, route overlay, and inference-mode switching support a repeatable walkthrough. A full accessibility audit has not been completed.
- **Local security:** the unused Android file server and its network access are disabled, and its generated token was removed from the current config. This does not remove it from Git history. The demo needs no remote service credentials; only use trusted models and CAD files.

D2 reflection identified single-machine testing and late packaging as weaknesses. D3 adds a repeatable campus workflow, CAD regression tests, geometry checks, and explicit overload reporting. Independent-machine validation remains open. D4 prioritizes clearer setup and controls and less integration boilerplate.
