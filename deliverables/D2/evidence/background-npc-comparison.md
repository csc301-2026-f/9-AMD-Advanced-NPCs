20,000 Mass agents, 10 decisions/s each (target 200,000/s), frame cap 60 FPS, 30 s capture after warm-up. CPU: AMD Ryzen 5 7600X 6-Core Processor; GPU: AMD Radeon RX 7800 XT (RADV NAVI32).

| Metric | Without API (per-agent sync) | With background NPC API |
| --- | ---: | ---: |
| Frame rate (FPS) | 10.4 | 60.0 |
| Mean frame time (ms) | 95.5 | 16.7 |
| p99 frame time (ms) | 101.2 | 16.7 |
| Game-thread inference per frame (ms) | 92.99 | 1.57 |
| Decisions per second | 199,272 | 199,979 |
| Agents per model call | 1.0 | 62.9 |
| Inference time per second of play (ms) | 970 | 327 |
| Observation-to-action latency (ms) | 0.0 | 15.8 |
| Dropped requests | 0 | 0 |
