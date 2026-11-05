20-second video load test: 10,000 cylinder students, target 60 decisions/s per agent, background maximum batch 256 with 2 workers, 1920×1080, 60 FPS cap. Same FP32 ONNX model and CPU runtime in both modes. Six-second measurements after six-second warmups; no video capture running during these measurements.

| Metric | Synchronous | Background |
| --- | ---: | ---: |
| FPS | 18.0 | 60.0 |
| Game-thread inference (ms/frame) | 50.82 | 2.30 |
| Completed decisions/s | 179,885 | 245,557 |
| Mean action latency (ms) | 0.01 | 15.66 |
| Dropped rows | 0 | 0 |

This is an intentional stress workload. Neither mode reaches the 600,000 decisions/s target; the live HUD shows achieved throughput. Background inference preserves rendering responsiveness while results arrive asynchronously. These short local measurements are not a general hardware benchmark, and capture may affect performance.
