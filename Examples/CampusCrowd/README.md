# North Campus crowd demo

Up to 100,000 Mass students walk a 1.3 × 1.4 km campus. Each student asks a neural network for a steering decision 10 times per second. Schola batches those requests and runs them on background worker threads.

```bash
export UE_ENGINE_ROOT=/path/to/UnrealEngine-5.8   # required
Examples/CampusCrowd/Scripts/build.sh
Examples/CampusCrowd/Scripts/run.sh -Agents=50000
```

## Performance

AMD Ryzen 5 7600X and Radeon RX 7800 XT, 1600×900, 60 FPS cap. Each student makes 10 decisions/s. Two inference workers handle batches of up to 64 rows.

| Students | FPS | Decisions/s (of target) | Game-thread inference | Crowd movement | Worker model time | Observation → action |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2,000 | 60 | 20,000 (100%) | 0.3 ms | 1.1 ms | 36 ms/s | 17 ms |
| 20,000 | 60 | 200,000 (100%) | 1.9 ms | 1.7 ms | 341 ms/s | 16 ms |
| 50,000 | 60 | 499,000 (100%) | 4.4 ms | 2.8 ms | 761 ms/s | 15 ms |
| 100,000 | 36 | 895,000 (90%) | 15.8 ms | 5.1 ms | 1,328 ms/s | 45 ms |

Game-thread times are per frame. Worker model time is summed across both workers, out of 2,000 ms/s available. At 100,000 students, the game thread rather than the model limits the frame rate. ScholaMass's per-frame bookkeeping takes 15.8 ms of each frame, and the per-frame row cap then limits how many decisions get submitted.

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
