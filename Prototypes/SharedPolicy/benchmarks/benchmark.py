"""Correctness-gated smoke benchmark for shared-policy architectures A–E."""

from __future__ import annotations

import argparse
import json
import platform
import random
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import numpy as np
import numpy.typing as npt
import onnx
import onnxruntime as ort

from benchmarks.models import DEFAULT_CONFIG_PATH, load_benchmark_config
from benchmarks.runtime import (
    SUPPORTED_ARCHITECTURES,
    BenchmarkRuntime,
    RunResult,
    load_runtime_settings,
)


DEFAULT_ARTIFACTS_DIR = Path(__file__).with_name("artifacts")
DEFAULT_SMOKE_OUTPUT = Path(__file__).with_name("runs") / "smoke.json"


@dataclass(frozen=True)
class WorkloadSettings:
    """Batch sizes and first-expert proportions requested by the experiment."""

    batch_sizes: tuple[int, ...]
    expert_0_fractions: tuple[float, ...]


@dataclass(frozen=True)
class MeasurementSettings:
    """Warm-up and repeated-request counts for the full experiment."""

    warmup_requests: int
    measured_requests: int
    trials: int


@dataclass(frozen=True)
class Tolerances:
    """Numerical equivalence limits applied before any timing is accepted."""

    relative: float
    absolute: float


@dataclass(frozen=True)
class ExperimentSettings:
    workload: WorkloadSettings
    measurement: MeasurementSettings
    tolerances: Tolerances


def _positive_int(name: str, value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{name} must be a positive integer")
    return value


def _fraction(name: str, value: object) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be a number between 0 and 1")
    result = float(value)
    if not 0.0 <= result <= 1.0:
        raise ValueError(f"{name} must be between 0 and 1")
    return result


def _positive_number(name: str, value: object) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be a positive number")
    result = float(value)
    if result <= 0:
        raise ValueError(f"{name} must be a positive number")
    return result


def load_experiment_settings(config_path: str | Path) -> ExperimentSettings:
    """Load and validate workload, measurement, and tolerance configuration."""

    path = Path(config_path)
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
        workloads = raw["workloads"]
        measurement = raw["measurement"]
        tolerances = raw["tolerances"]
        batch_sizes = tuple(
            _positive_int(f"workloads.batch_sizes[{index}]", value)
            for index, value in enumerate(workloads["batch_sizes"])
        )
        fractions = tuple(
            _fraction(f"workloads.expert_0_fractions[{index}]", value)
            for index, value in enumerate(workloads["expert_0_fractions"])
        )
        settings = ExperimentSettings(
            workload=WorkloadSettings(
                batch_sizes=batch_sizes,
                expert_0_fractions=fractions,
            ),
            measurement=MeasurementSettings(
                warmup_requests=_positive_int(
                    "measurement.warmup_requests",
                    measurement["warmup_requests"],
                ),
                measured_requests=_positive_int(
                    "measurement.measured_requests",
                    measurement["measured_requests"],
                ),
                trials=_positive_int(
                    "measurement.trials",
                    measurement["trials"],
                ),
            ),
            tolerances=Tolerances(
                relative=_positive_number(
                    "tolerances.relative",
                    tolerances["relative"],
                ),
                absolute=_positive_number(
                    "tolerances.absolute",
                    tolerances["absolute"],
                ),
            ),
        )
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid experiment config at {path}: {error}") from error

    if not settings.workload.batch_sizes:
        raise ValueError("workloads.batch_sizes must not be empty")
    if not settings.workload.expert_0_fractions:
        raise ValueError("workloads.expert_0_fractions must not be empty")
    return settings


def generate_workload(
    seed: int,
    observation_size: int,
    roles: tuple[str, ...],
    batch_size: int,
    expert_0_fraction: float,
) -> tuple[npt.NDArray[np.float32], tuple[str, ...]]:
    """Create deterministic observations and a shuffled two-expert role mix."""

    if len(roles) != 2:
        raise ValueError("the smoke workload currently requires exactly two experts")
    _positive_int("batch_size", batch_size)
    fraction = _fraction("expert_0_fraction", expert_0_fraction)
    expert_0_count = round(batch_size * fraction)
    role_values = np.array(
        [roles[0]] * expert_0_count
        + [roles[1]] * (batch_size - expert_0_count),
        dtype=object,
    )
    rng = np.random.default_rng(seed)
    rng.shuffle(role_values)
    observations = rng.normal(
        size=(batch_size, observation_size),
    ).astype(np.float32)
    return observations, tuple(str(role) for role in role_values)


def assert_equivalent(
    architecture: str,
    actual: npt.NDArray[np.float32],
    expected: npt.NDArray[np.float32],
    tolerances: Tolerances,
) -> None:
    """Stop the benchmark if one architecture violates the action contract."""

    try:
        np.testing.assert_allclose(
            actual,
            expected,
            rtol=tolerances.relative,
            atol=tolerances.absolute,
        )
    except AssertionError as error:
        raise RuntimeError(
            f"correctness gate failed for architecture {architecture}"
        ) from error


def summarize_samples(
    request_samples_ns: Sequence[int],
    model_samples_ns: Sequence[int],
    batch_size: int,
) -> dict[str, float | int]:
    """Summarize retained raw samples without discarding their source values."""

    if not request_samples_ns or len(request_samples_ns) != len(model_samples_ns):
        raise ValueError("request and model samples must be nonempty and aligned")
    request_values = np.asarray(request_samples_ns, dtype=np.float64)
    model_values = np.asarray(model_samples_ns, dtype=np.float64)
    routing_values = request_values - model_values
    p50_request_ns = float(np.percentile(request_values, 50))
    return {
        "sample_count": len(request_samples_ns),
        "p50_request_ns": p50_request_ns,
        "p95_request_ns": float(np.percentile(request_values, 95)),
        "p50_model_ns": float(np.percentile(model_values, 50)),
        "p95_model_ns": float(np.percentile(model_values, 95)),
        "p50_routing_overhead_ns": float(np.percentile(routing_values, 50)),
        "rows_per_second_at_p50": float(batch_size * 1_000_000_000)
        / p50_request_ns,
    }


def _timed_request(
    runtime: BenchmarkRuntime,
    architecture: str,
    observations: npt.NDArray[np.float32],
    roles: tuple[str, ...],
) -> tuple[RunResult, int]:
    started_ns = time.perf_counter_ns()
    result = runtime.run(architecture, observations, roles)
    return result, time.perf_counter_ns() - started_ns


def _serialize_calls(result: RunResult) -> list[dict[str, int | str]]:
    return [
        {
            "artifact": call.artifact,
            "rows": call.rows,
            "duration_ns": call.duration_ns,
        }
        for call in result.calls
    ]


def _smoke_selection(settings: ExperimentSettings) -> tuple[int, float]:
    """Choose one useful mixed workload without running the full matrix."""

    batch_size = next(
        (size for size in settings.workload.batch_sizes if size > 1),
        settings.workload.batch_sizes[0],
    )
    fraction = min(
        settings.workload.expert_0_fractions,
        key=lambda value: abs(value - 0.5),
    )
    return batch_size, fraction


def run_smoke_benchmark(
    config_path: str | Path,
    artifacts_dir: str | Path,
) -> dict[str, object]:
    """Run a short correctness-gated benchmark suitable for harness validation."""

    config = load_benchmark_config(config_path)
    experiment = load_experiment_settings(config_path)
    runtime_settings = load_runtime_settings(config_path)
    profile_name = "small" if "small" in config.profiles else next(iter(config.profiles))
    batch_size, expert_0_fraction = _smoke_selection(experiment)
    observations, roles = generate_workload(
        config.seed,
        config.observation_size,
        config.roles,
        batch_size,
        expert_0_fraction,
    )

    initialization_started_ns = time.perf_counter_ns()
    runtime = BenchmarkRuntime.from_paths(
        config_path,
        artifacts_dir,
        profile_name,
    )
    initialization_ns = time.perf_counter_ns() - initialization_started_ns

    first_runs: dict[str, dict[str, object]] = {}
    baseline: npt.NDArray[np.float32] | None = None
    for architecture in SUPPORTED_ARCHITECTURES:
        result, duration_ns = _timed_request(
            runtime,
            architecture,
            observations,
            roles,
        )
        if baseline is None:
            baseline = result.actions
        else:
            assert_equivalent(
                architecture,
                result.actions,
                baseline,
                experiment.tolerances,
            )
        first_runs[architecture] = {
            "request_ns": duration_ns,
            "calls": _serialize_calls(result),
        }

    warmup_requests = min(experiment.measurement.warmup_requests, 2)
    measured_requests = min(experiment.measurement.measured_requests, 5)
    order_rng = random.Random(config.seed)
    for _ in range(warmup_requests):
        order = list(SUPPORTED_ARCHITECTURES)
        order_rng.shuffle(order)
        for architecture in order:
            runtime.run(architecture, observations, roles)

    raw_results: dict[str, dict[str, object]] = {
        architecture: {
            "request_samples_ns": [],
            "model_samples_ns": [],
            "call_samples": [],
        }
        for architecture in SUPPORTED_ARCHITECTURES
    }
    measured_orders: list[list[str]] = []
    for _ in range(measured_requests):
        order = list(SUPPORTED_ARCHITECTURES)
        order_rng.shuffle(order)
        measured_orders.append(order)
        for architecture in order:
            result, request_ns = _timed_request(
                runtime,
                architecture,
                observations,
                roles,
            )
            model_ns = sum(call.duration_ns for call in result.calls)
            raw_results[architecture]["request_samples_ns"].append(request_ns)
            raw_results[architecture]["model_samples_ns"].append(model_ns)
            raw_results[architecture]["call_samples"].append(
                _serialize_calls(result)
            )

    results: dict[str, object] = {}
    for architecture, raw in raw_results.items():
        request_samples = raw["request_samples_ns"]
        model_samples = raw["model_samples_ns"]
        call_samples = raw["call_samples"]
        call_counts = {len(calls) for calls in call_samples}
        if len(call_counts) != 1:
            raise RuntimeError(
                f"architecture {architecture} changed model-call count"
            )
        results[architecture] = {
            **raw,
            "call_count_per_request": call_counts.pop(),
            "summary": summarize_samples(
                request_samples,
                model_samples,
                batch_size,
            ),
        }

    manifest_path = Path(artifacts_dir) / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    role_counts = {role: roles.count(role) for role in config.roles}
    return {
        "mode": "smoke",
        "seed": config.seed,
        "profile": profile_name,
        "workload": {
            "batch_size": batch_size,
            "requested_expert_0_fraction": expert_0_fraction,
            "actual_role_counts": role_counts,
        },
        "measurement": {
            "trials": 1,
            "warmup_requests_per_architecture": warmup_requests,
            "measured_requests_per_architecture": measured_requests,
            "candidate_orders": measured_orders,
        },
        "runtime": {
            "provider": runtime_settings.provider,
            "execution_mode": runtime_settings.execution_mode,
            "intra_op_threads": runtime_settings.intra_op_threads,
            "inter_op_threads": runtime_settings.inter_op_threads,
            "graph_optimization": runtime_settings.graph_optimization,
            "session_initialization_ns": initialization_ns,
        },
        "environment": {
            "python": platform.python_version(),
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "numpy": np.__version__,
            "onnx": onnx.__version__,
            "onnxruntime": ort.__version__,
        },
        "artifact_manifest": {
            "format_version": manifest["format_version"],
            "opset": manifest["opset"],
            "profile": manifest["profiles"][profile_name],
        },
        "correctness": {
            "passed": True,
            "relative_tolerance": experiment.tolerances.relative,
            "absolute_tolerance": experiment.tolerances.absolute,
            "first_runs": first_runs,
        },
        "results": results,
    }


def write_report(report: dict[str, object], output_path: str | Path) -> Path:
    """Write a completed report only after every benchmark stage succeeds."""

    destination = Path(output_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return destination


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run shared-policy architecture benchmarks.",
    )
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_PATH)
    parser.add_argument(
        "--artifacts-dir",
        type=Path,
        default=DEFAULT_ARTIFACTS_DIR,
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_SMOKE_OUTPUT)
    parser.add_argument(
        "--smoke",
        action="store_true",
        help="Run the short harness-validation workload.",
    )
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    if not args.smoke:
        raise SystemExit("Only --smoke is implemented in this increment")
    report = run_smoke_benchmark(args.config, args.artifacts_dir)
    output_path = write_report(report, args.output)
    print(f"Correctness passed; smoke report: {output_path}")


if __name__ == "__main__":
    main()
