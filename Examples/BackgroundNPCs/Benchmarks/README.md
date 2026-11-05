# Crowd inference: CPU versus ROCm

Measured on 2026-10-27 within the requested 20-minute testing window.

**ROCm is worth a backend experiment for batches of 512–1,024. Start with one
worker and `GPU_MAX_HW_QUEUES=1`.** The current 64-row batches favor CPU.
These are inference microbenchmarks; the Unreal demo still uses its CPU backend.

## Hardware and method

- AMD Ryzen 5 7600X; AMD Radeon RX 7800 XT, `gfx1101`, 16 GB VRAM.
- Existing PyTorch `2.14.1+rocm7.2`, HIP `7.2.53211`; ONNX Runtime `1.30.0` CPU.
- Loaded the actual initializers from `Content/Models/crowd_policy.onnx` into
  the equivalent PyTorch graph: Gemm/ReLU, Gemm/ReLU, Gemm/Tanh, scaled wander,
  skip MatMul, Add. FP32 throughout, TF32 disabled.
- CPU: one sequential ORT session per worker, intra/inter-op threads both 1.
- GPU: one stream/context per worker and batch shape. Compared ordinary eager
  execution with captured HIP graphs, exposed through PyTorch's `cuda` API.
  [PyTorch documents that naming for ROCm](https://docs.pytorch.org/docs/main/notes/hip.html).
- End-to-end GPU timings include CPU input staging into persistent pinned memory,
  upload, inference, output readback, stream synchronization, and copying into the
  CPU destination. Resident-only timings are reported separately in the raw data.
- Warmed contexts before measuring. Saturated throughput uses three timed repeats;
  tables show the median throughput. Initial and tuned GPU sweeps use 0.5 s/repeat;
  the fresh CPU control uses 0.4 s/repeat.
- Paced bursts use 3,333 rows at 60 Hz, approximating 20,000 agents at 10 Hz.
  Each configuration has 10 warm-up frames and 120 measured frames. Worker tasks
  divide the full batches and final partial batch; burst timings include Python
  executor dispatch and waiting for all workers.
- Every context was checked against ORT CPU output. Maximum absolute GPU error
  across the saved runs was below `4e-7`; all tolerance checks passed.
- Tests ran sequentially on the desktop, without a concurrent Unreal rendering
  workload. GPU access required running outside the agent filesystem/device sandbox.

## Throughput with staging and transfers

Millions of rows/second. CPU columns use the fresh-process control; GPU columns
use the final repeat with `GPU_MAX_HW_QUEUES=1` and captured graphs.

| Maximum batch | CPU, 1 worker | CPU, 2 workers | CPU, 4 workers | ROCm, 1 worker |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 0.73 | 1.46 | 2.53 | 0.53 |
| 128 | 0.80 | 1.68 | 3.28 | 1.15 |
| 256 | 0.84 | 1.71 | 3.35 | 2.25 |
| 512 | 0.80 | 1.64 | 3.46 | 4.23 |
| 1,024 | 0.85 | 1.67 | 3.43 | 6.63 |

One GPU worker beats one CPU worker from 128 rows, two CPU workers from 256,
and four CPU workers from 512 in these measurements. Batch 128 is a weak
starting point: it still loses to two CPU workers and consumes more CPU time
in the paced workload. GPU acceleration also worked without graph capture:
the initial eager end-to-end run reached 3.50 M rows/s at 512 and 6.20 M at 1,024
with one worker and default queue settings.

## Default crowd workload at 60 Hz

Inference completion time for each 3,333-row burst, excluding gather/apply/rendering.
Process CPU time sums every thread, including runtime helpers. The CPU/frame
column includes the paced interval and is averaged across warm-up plus measured
frames; p50/p99 use only measured frames.

| Backend | Batch | Workers | p50 completion (ms) | p99 completion (ms) | Process CPU/frame (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| ORT CPU | 64 | 2 | 2.32 | 4.30 | 4.66 |
| ORT CPU | 256 | 4 | 1.39 | 1.73 | 4.60 |
| ORT CPU | 1,024 | 2 | 2.75 | 4.17 | 4.77 |
| ROCm, one hardware queue | 256 | 1 | 2.03 | 2.30 | 3.47 |
| ROCm, one hardware queue | 512 | 1 | 1.31 | 1.52 | 2.17 |
| ROCm, one hardware queue | 1,024 | 1 | 1.12 | 1.37 | 1.90 |
| ROCm, one hardware queue | 1,024 | 2 | 0.94 | 1.17 | 2.32 |

At 1,024 rows, one GPU worker uses about 59% less process CPU time than the
two-worker CPU/64 configuration, saving roughly 0.17 CPU core at 60 Hz.
Two GPU workers reduce completion latency slightly but use more CPU. One worker
has ample capacity for the target 200,000 decisions/s. All saved paced runs
finished each inference burst within 16.67 ms.

## The queue setting matters

With default ROCm settings, GPU work left approximately one CPU core busy during
the gaps between bursts in this process. The focused accounting run measured
17.54 ms of total process CPU time per 60 Hz frame for GPU batch 1,024, worker 1,
despite only 0.92 ms median inference completion. This is a net CPU-budget loss
against the fresh CPU control.

`AMD_DIRECT_DISPATCH=0` did not resolve it: 18.35 ms CPU/frame at batch 1,024,
worker 1. `GPU_MAX_HW_QUEUES=1` reduced it to 1.65 ms CPU/frame in the first
queue experiment and 1.90 ms in the final repeat. These observations establish
the mitigation for this runtime/device combination, not the internal cause.
The [ROCm 7.2 source defines this knob as the maximum number of hardware queues
per device](https://github.com/ROCm/clr/blob/rocm-7.2.0/rocclr/utils/flags.hpp).

Use the setting in the inference process environment before ROCm initialization.
No persistent system settings were changed by these tests. The initial mixed
CPU/GPU runs also retained ROCm helper activity during later CPU configurations;
use the fresh CPU control for CPU-budget comparisons.

## Settings and implementation implications

Suggested first backend configuration:

```text
GPU_MAX_HW_QUEUES=1
WorkerCount=1
MaxBatchSize=1024             # also try 512
MaxInFlightBatchesPerWorker=2
Dispatch=SharedQueue
MaxAssemblyWaitMs=0
```

Only batch size, workers, and the runtime queue setting were benchmarked here.
The other values retain scheduler defaults and were not independently tuned.
The current backend rejects GPU runtimes, so these values require a ROCm backend
implementation; there is no existing GPU command-line switch in the demo.

At 20,000 agents, 10 Hz, 60 FPS, about 3,333 compatible rows arrive per frame.
That supplies several 512/1,024 batches without cross-frame assembly waits.
Keep models and weights resident, reuse device and pinned host buffers, and
cache graph/shape variants or pad partial batches. This harness keeps the full
and tail shape contexts warm; graph creation and repeated shape changes are
outside the timings. A backend that rebuilds graphs or allocates each call
needs new measurements.

The demo's 4,096 `MaxRowsPerFrame` and scheduler `MaxPendingRows` limits remain
relevant. At 60 FPS, the gather limit alone bounds submissions to 245,760 rows/s.
Raise admission limits coherently before attempting population scaling above
that rate. The default 8 MiB pending-byte cap holds about 31,775 observation/action
rows at 264 bytes/row, independently of runtime staging and device memory.

Unreal applies completed requests on a later frame. Faster backend execution
can save worker CPU and increase capacity while leaving action latency near
one frame. The current demo already reaches its 60 FPS cap on CPU. Rendering
contention, finite-value validation/row packing in the production provider,
scheduler locking, and actual game-thread/frame-time gains need an integrated
run before deciding to ship a GPU default.

## Reproduce

The harness needs NumPy, CPU ONNX Runtime, and a ROCm PyTorch installation.
`--onnx-python` selects a Python with ONNX and NumPy for reading initializers;
it can be the same interpreter. The optional `--ort-package` points at an
existing installed package without changing environments.

From `Examples/BackgroundNPCs`, using the existing installations on this machine:

```bash
BENCH_PY=/home/future/Code/embeddingtest/.venv/bin/python
ONNX_PY=/home/future/.cache/schola-bg/pyenv/bin/python
ORT_PACKAGE=/home/future/.cache/uv/archive-v0/z28EIH-BCNoXEArf

# Fresh CPU control: no preceding GPU inference in this process.
"$BENCH_PY" Tools/benchmark_rocm.py --onnx-python "$ONNX_PY" \
  --ort-package "$ORT_PACKAGE" --output /tmp/crowd-cpu.json \
  --batches 64 128 256 512 1024 --workers 1 2 4 --seconds .4 \
  --repeats 3 --burst-frames 120 --modes ort_cpu

# Tuned GPU sweep and paced bursts.
GPU_MAX_HW_QUEUES=1 "$BENCH_PY" Tools/benchmark_rocm.py \
  --onnx-python "$ONNX_PY" --ort-package "$ORT_PACKAGE" \
  --output /tmp/crowd-gpu.json --batches 64 128 256 512 1024 \
  --workers 1 2 --seconds .5 --repeats 3 --burst-frames 120 --modes graph_e2e
```

## Saved evidence

- `rocm-crowd-2026-10-27.json`: initial 81-configuration throughput sweep,
  including resident/eager/graph comparisons and up to four workers.
- `rocm-crowd-bursts-2026-10-27.json`: initial 35 paced burst configurations.
- `rocm-crowd-cpu-accounting-2026-10-27.json`: focused CPU accounting,
  including the full paced interval and per-worker CPU time.
- `rocm-crowd-dispatch-off-2026-10-27.json`: unsuccessful dispatch-setting test.
- `rocm-crowd-one-queue-2026-10-27.json`: first successful queue-setting test.
- `rocm-crowd-cpu-control-2026-10-27.json`: fresh CPU control used in the tables.
- `rocm-crowd-tuned-2026-10-27.json`: final tuned GPU repeat used in the tables.

JSON files retain per-repeat throughputs, correctness errors, model hash,
runtime versions, and per-frame burst timing samples. Later focused runs also
record the selected runtime environment variables and CPU accounting.
