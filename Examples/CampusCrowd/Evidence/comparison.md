2,000 Mass agents, 10 decisions/s each (target 20,000/s), frame cap 60 FPS, 5 s capture after warm-up. CPU: AMD Ryzen 5 7600X 6-Core Processor; GPU: AMD Radeon RX 7800 XT (RADV NAVI32).

| Metric | Without API (per-agent sync) | With background NPC API |
| --- | ---: | ---: |
| Frame rate (FPS) | 60.0 | 60.0 |
| Mean frame time (ms) | 16.7 | 16.7 |
| p99 frame time (ms) | 16.8 | 16.8 |
| Game-thread inference per frame (ms) | 1.83 | 0.34 |
| Decisions per second | 19,990 | 19,996 |
| Agents per model call | 1.0 | 55.6 |
| Inference time per second of play (ms) | 110 | 37 |
| Observation-to-action latency (ms) | 0.0 | 16.5 |
| Dropped requests | 0 | 0 |
