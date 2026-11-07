"""Compare the crowd ONNX policy on ORT CPU and PyTorch ROCm.

Requires numpy, onnxruntime, and a ROCm build of torch. ONNX weights are
read by --onnx-python (which needs numpy and onnx). No Unreal integration.
GPU timings include synchronization; e2e modes also include CPU staging,
pinned host input upload, output readback, and a copy to the CPU destination.
Models and buffers persist between calls.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--model', type=Path, default=Path(__file__).resolve().parents[1] / 'Content/Models/crowd_policy.onnx')
parser.add_argument('--onnx-python', default=sys.executable)
parser.add_argument('--ort-package', help='Optional directory containing an installed onnxruntime package')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=.4)
parser.add_argument('--repeats', type=int, default=3)
parser.add_argument('--batches', type=int, nargs='+', default=[1, 32, 64, 128, 256, 512, 1024, 2048, 4096])
parser.add_argument('--workers', type=int, nargs='+', default=[1, 2, 4])
parser.add_argument('--modes', nargs='+', default=['ort_cpu', 'eager_resident', 'graph_resident', 'eager_e2e', 'graph_e2e'],
                    choices=['ort_cpu', 'eager_resident', 'graph_resident', 'eager_e2e', 'graph_e2e'])
parser.add_argument('--skip-sweep', action='store_true')
parser.add_argument('--burst-frames', type=int, default=0, help='Also time paced 60 Hz inference bursts')
parser.add_argument('--burst-rows', type=int, default=3333)
args = parser.parse_args()
if args.ort_package:
    sys.path.insert(0, args.ort_package)
import numpy as np
import onnxruntime as ort
import torch

torch.set_num_threads(1)
torch.set_num_interop_threads(1)
if not torch.version.hip or not torch.cuda.is_available():
    raise RuntimeError('A working ROCm PyTorch GPU is required; no CPU fallback')
torch.cuda.set_device(0)
torch.backends.cuda.matmul.allow_tf32 = False
torch.set_float32_matmul_precision('highest')

with tempfile.TemporaryDirectory(prefix='crowd-rocm-') as directory:
    weights_path = Path(directory) / 'weights.npz'
    subprocess.run([args.onnx_python, '-c',
        'import sys,onnx,numpy as np; from onnx import numpy_helper; '
        'm=onnx.load(sys.argv[1]); '
        'np.savez(sys.argv[2],**{t.name:numpy_helper.to_array(t) for t in m.graph.initializer})',
        str(args.model), str(weights_path)], check=True)
    with np.load(weights_path) as data:
        weights = {k: data[k].copy() for k in data.files}


class Policy(torch.nn.Module):
    def __init__(self):
        super().__init__()
        for key, value in weights.items():
            self.register_buffer(key, torch.from_numpy(value).cuda())

    def forward(self, x):
        h = torch.relu(torch.addmm(self.b1, x, self.w1))
        h = torch.relu(torch.addmm(self.b2, h, self.w2))
        h = torch.tanh(torch.addmm(self.b3, h, self.w3))
        return h * self.scale + x @ self.skip


def cpu_session():
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    return ort.InferenceSession(str(args.model), options, providers=['CPUExecutionProvider'])


class Context:
    def __init__(self, batch, mode, seed):
        self.mode = mode
        self.obs = np.random.default_rng(seed).uniform(-1, 1, (batch, 64)).astype(np.float32)
        self.obs_tensor = torch.from_numpy(self.obs)
        self.result = np.empty((batch, 2), dtype=np.float32)
        if mode == 'ort_cpu':
            self.session = cpu_session()
        else:
            self.stream = torch.cuda.Stream()
            self.host_in = torch.empty((batch, 64), pin_memory=True)
            self.host_in.copy_(torch.from_numpy(self.obs))
            self.host_out = torch.empty((batch, 2), pin_memory=True)
            self.x = self.host_in.cuda()
            self.model = Policy().eval()
            self.stream.wait_stream(torch.cuda.current_stream())
            with torch.cuda.stream(self.stream), torch.inference_mode():
                for _ in range(20):
                    self.y = self.model(self.x)
            self.stream.synchronize()
            if mode.startswith('graph'):
                self.graph = torch.cuda.CUDAGraph()
                with torch.inference_mode(), torch.cuda.graph(self.graph, stream=self.stream):
                    self.y = self.model(self.x)
                self.stream.synchronize()
        self.call()

    def call(self):
        if self.mode == 'ort_cpu':
            self.result = self.session.run(None, {'obs': self.obs})[0]
            return
        with torch.cuda.stream(self.stream), torch.inference_mode():
            if self.mode.endswith('e2e'):
                self.host_in.copy_(self.obs_tensor)
                self.x.copy_(self.host_in, non_blocking=True)
            if self.mode.startswith('graph'):
                self.graph.replay()
            else:
                self.y = self.model(self.x)
            if self.mode.endswith('e2e'):
                self.host_out.copy_(self.y, non_blocking=True)
        self.stream.synchronize()
        if self.mode.endswith('e2e'):
            np.copyto(self.result, self.host_out.numpy())

    def check(self, reference):
        self.call()
        actual = self.result if self.mode == 'ort_cpu' else self.y.cpu().numpy()
        expected = reference.run(None, {'obs': self.obs})[0]
        np.testing.assert_allclose(actual, expected, rtol=2e-4, atol=2e-5)
        if self.mode.endswith('e2e'):
            np.testing.assert_allclose(self.result, expected, rtol=2e-4, atol=2e-5)
        return float(np.max(np.abs(actual - expected)))


result = {'metadata': {
    'utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    'platform': platform.platform(), 'python': sys.version, 'torch': torch.__version__,
    'cpu': next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                 if line.startswith('model name')), platform.processor()),
    'hip': torch.version.hip, 'ort': ort.__version__,
    'gpu': str(torch.cuda.get_device_properties(0)),
    'model_sha256': hashlib.sha256(args.model.read_bytes()).hexdigest(),
    'precision': 'float32; TF32 disabled', 'seconds_per_repeat': args.seconds,
    'repeats': args.repeats, 'cpu_intra_op_threads_per_worker': 1,
    'batches': args.batches, 'workers': args.workers,
    'burst_rows': args.burst_rows, 'burst_frames': args.burst_frames,
    'modes': args.modes,
    'runtime_environment': {key: os.environ.get(key) for key in
        ['AMD_DIRECT_DISPATCH', 'ROC_ACTIVE_WAIT_TIMEOUT', 'HSA_ENABLE_INTERRUPT', 'GPU_MAX_HW_QUEUES']},
    'note': 'Inference microbenchmark; excludes Unreal gather/apply/rendering. Each GPU call synchronizes. e2e modes include input/output host staging and use persistent pinned buffers.'},
    'results': []}
reference = cpu_session()


def save():
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')


for batch in ([] if args.skip_sweep else args.batches):
    for mode in args.modes:
        max_workers = max(args.workers) if mode in ['ort_cpu', 'graph_e2e'] else 1
        contexts = [Context(batch, mode, i + batch) for i in range(max_workers)]
        error = max(c.check(reference) for c in contexts)
        for workers in (args.workers if mode in ['ort_cpu', 'graph_e2e'] else [1]):
            runs = []
            for repeat in range(args.repeats):
                barrier = threading.Barrier(workers + 1)

                def run(context):
                    torch.cuda.set_device(0)
                    barrier.wait()
                    start = time.perf_counter()
                    count = 0
                    samples = []
                    while time.perf_counter() - start < args.seconds:
                        before = time.perf_counter_ns()
                        context.call()
                        samples.append((time.perf_counter_ns() - before) / 1000)
                        count += 1
                    return count, time.perf_counter() - start, samples

                with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
                    futures = [executor.submit(run, c) for c in contexts[:workers]]
                    barrier.wait()
                    values = [f.result() for f in futures]
                durations = [s for _, _, samples in values for s in samples]
                runs.append({'rows_per_second': sum(v[0] for v in values) * batch / max(v[1] for v in values),
                             'call_p50_us': float(np.percentile(durations, 50)),
                             'call_p99_us': float(np.percentile(durations, 99))})
            row = {'batch': batch, 'mode': mode, 'workers': workers,
                   'rows_per_second': statistics.median(v['rows_per_second'] for v in runs),
                   'call_p50_us': statistics.median(v['call_p50_us'] for v in runs),
                   'call_p99_us': statistics.median(v['call_p99_us'] for v in runs),
                   'max_abs_error': error, 'runs': runs}
            result['results'].append(row)
            save()
            print(json.dumps({k: v for k, v in row.items() if k != 'runs'}), flush=True)
        del contexts
save()

if args.burst_frames:
    result['bursts'] = []
    for batch in args.batches:
        sizes = [batch] * (args.burst_rows // batch)
        if args.burst_rows % batch:
            sizes.append(args.burst_rows % batch)
        for mode in [m for m in args.modes if m in ['ort_cpu', 'eager_e2e', 'graph_e2e']]:
            for workers in (args.workers if mode != 'eager_e2e' else [1]):
                contexts = [{size: Context(size, mode, size + w) for size in set(sizes)} for w in range(workers)]
                error = max(c.check(reference) for lane in contexts for c in lane.values())
                lanes = [sizes[w::workers] for w in range(workers)]

                def run_lane(w):
                    before = time.thread_time()
                    for size in lanes[w]:
                        contexts[w][size].call()
                    return (time.thread_time() - before) * 1000

                samples = []
                cpu_samples = []
                worker_cpu_samples = []
                with concurrent.futures.ThreadPoolExecutor(max_workers=workers,
                        initializer=lambda: torch.cuda.set_device(0)) as executor:
                    deadline = time.perf_counter()
                    process_start = time.process_time()
                    for frame in range(args.burst_frames + 10):
                        delay = deadline - time.perf_counter()
                        if delay > 0:
                            time.sleep(delay)
                        before = time.perf_counter()
                        cpu_before = time.process_time()
                        futures = [executor.submit(run_lane, w) for w in range(workers) if lanes[w]]
                        worker_cpu = sum(future.result() for future in futures)
                        elapsed = time.perf_counter() - before
                        if frame >= 10:
                            samples.append(elapsed * 1000)
                            cpu_samples.append((time.process_time() - cpu_before) * 1000)
                            worker_cpu_samples.append(worker_cpu)
                        deadline = max(deadline + 1 / 60, time.perf_counter())
                row = {'batch': batch, 'mode': mode, 'workers': workers,
                       'rows_per_frame': args.burst_rows, 'frames': args.burst_frames,
                       'frame_inference_p50_ms': float(np.percentile(samples, 50)),
                       'frame_inference_p99_ms': float(np.percentile(samples, 99)),
                       'process_cpu_ms_per_frame_mean': statistics.mean(cpu_samples),
                       'worker_cpu_ms_per_frame_mean': statistics.mean(worker_cpu_samples),
                       'process_cpu_ms_per_frame_including_pacing':
                           (time.process_time() - process_start) * 1000 / (args.burst_frames + 10),
                       'missed_16_67ms': sum(s > 1000 / 60 for s in samples),
                       'max_abs_error': error, 'samples_ms': samples}
                result['bursts'].append(row)
                save()
                print(json.dumps({k: v for k, v in row.items() if k != 'samples_ms'}), flush=True)
                del contexts
