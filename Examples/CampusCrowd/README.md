# North Campus crowd demo

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
