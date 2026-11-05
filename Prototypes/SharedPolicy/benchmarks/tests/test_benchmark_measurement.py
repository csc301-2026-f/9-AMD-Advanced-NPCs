"""Tests for smoke workload generation, correctness gating, and timing reports."""

from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path

import numpy as np
import pytest

from benchmarks import benchmark
from benchmarks.benchmark import (
    Tolerances,
    TrialSpec,
    assert_equivalent,
    generate_workload,
    load_experiment_settings,
    run_benchmark_trial,
    run_smoke_benchmark,
    summarize_samples,
    write_report,
)
from benchmarks.export_models import export_models
from benchmarks.runtime import RunResult


CONFIG_PATH = Path(__file__).parents[1] / "config.json"


def test_loads_workload_measurement_and_tolerances() -> None:
    settings = load_experiment_settings(CONFIG_PATH)

    assert settings.workload.batch_sizes == (1, 16, 64, 256)
    assert settings.workload.expert_0_fractions == (1.0, 0.5, 0.1, 0.0)
    assert settings.measurement.warmup_requests == 20
    assert settings.measurement.measured_requests == 200
    assert settings.measurement.trials == 3
    assert settings.tolerances == Tolerances(
        relative=0.0001,
        absolute=0.00001,
    )


def test_workload_is_deterministic_shuffled_and_has_requested_counts() -> None:
    first_observations, first_roles = generate_workload(
        seed=301,
        observation_size=64,
        roles=("expert_0", "expert_1"),
        batch_size=16,
        expert_0_fraction=0.5,
    )
    second_observations, second_roles = generate_workload(
        seed=301,
        observation_size=64,
        roles=("expert_0", "expert_1"),
        batch_size=16,
        expert_0_fraction=0.5,
    )

    np.testing.assert_array_equal(first_observations, second_observations)
    assert first_roles == second_roles
    assert first_roles.count("expert_0") == 8
    assert first_roles.count("expert_1") == 8
    assert first_roles != ("expert_0",) * 8 + ("expert_1",) * 8


@pytest.mark.parametrize(
    ("fraction", "expected_expert_0"),
    [(0.0, 0), (1.0, 4)],
)
def test_workload_handles_homogeneous_expert_mixes(
    fraction: float,
    expected_expert_0: int,
) -> None:
    _, roles = generate_workload(
        seed=301,
        observation_size=64,
        roles=("expert_0", "expert_1"),
        batch_size=4,
        expert_0_fraction=fraction,
    )

    assert roles.count("expert_0") == expected_expert_0


def test_summarizes_known_samples_and_throughput() -> None:
    summary = summarize_samples(
        request_samples_ns=[100, 200, 300],
        model_samples_ns=[60, 120, 180],
        batch_size=4,
    )

    assert summary["sample_count"] == 3
    assert summary["p50_request_ns"] == 200.0
    assert summary["p95_request_ns"] == 290.0
    assert summary["p50_model_ns"] == 120.0
    assert summary["p50_routing_overhead_ns"] == 80.0
    assert summary["rows_per_second_at_p50"] == 20_000_000.0


def test_correctness_gate_rejects_mismatched_actions() -> None:
    expected = np.zeros((2, 2), dtype=np.float32)
    actual = np.ones((2, 2), dtype=np.float32)

    with pytest.raises(RuntimeError, match="architecture C"):
        assert_equivalent(
            "C",
            actual,
            expected,
            Tolerances(relative=1e-4, absolute=1e-5),
        )


def test_rejects_unaligned_or_empty_measurement_samples() -> None:
    with pytest.raises(ValueError, match="nonempty and aligned"):
        summarize_samples([], [], batch_size=4)
    with pytest.raises(ValueError, match="nonempty and aligned"):
        summarize_samples([100], [50, 60], batch_size=4)


@pytest.mark.parametrize(
    ("section", "field", "value", "message"),
    [
        ("workloads", "batch_sizes", [], "must not be empty"),
        (
            "workloads",
            "expert_0_fractions",
            [1.2],
            "between 0 and 1",
        ),
        ("measurement", "warmup_requests", 0, "positive integer"),
    ],
)
def test_rejects_invalid_experiment_configuration(
    tmp_path: Path,
    section: str,
    field: str,
    value: object,
    message: str,
) -> None:
    raw = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    modified = deepcopy(raw)
    modified[section][field] = value
    config_path = tmp_path / "invalid-config.json"
    config_path.write_text(json.dumps(modified), encoding="utf-8")

    with pytest.raises(ValueError, match=message):
        load_experiment_settings(config_path)


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("profile_name", "", "must not be empty"),
        ("batch_size", 0, "positive integer"),
        ("expert_0_fraction", 1.1, "between 0 and 1"),
        ("warmup_requests", 0, "positive integer"),
        ("measured_requests", 0, "positive integer"),
        ("trial_index", -1, "nonnegative integer"),
        ("seed", -1, "nonnegative integer"),
    ],
)
def test_trial_spec_rejects_invalid_values(
    field: str,
    value: object,
    message: str,
) -> None:
    values = {
        "profile_name": "small",
        "batch_size": 16,
        "expert_0_fraction": 0.5,
        "warmup_requests": 2,
        "measured_requests": 5,
        "trial_index": 0,
        "seed": 301,
    }
    values[field] = value

    with pytest.raises(ValueError, match=message):
        TrialSpec(**values)


@pytest.mark.parametrize(
    ("overrides", "message"),
    [
        ({"measured_architectures": ()}, "must not be empty"),
        ({"measured_architectures": ("C", "C")}, "must be unique"),
        ({"measured_architectures": ("C", "Z")}, "unsupported measured"),
        ({"correctness_oracle": "Z"}, "correctness_oracle"),
    ],
)
def test_trial_spec_rejects_invalid_architecture_selection(
    overrides: dict[str, object],
    message: str,
) -> None:
    values = {
        "profile_name": "small",
        "batch_size": 16,
        "expert_0_fraction": 0.5,
        "warmup_requests": 2,
        "measured_requests": 5,
        "trial_index": 0,
        "seed": 301,
        **overrides,
    }

    with pytest.raises(ValueError, match=message):
        TrialSpec(**values)


def test_trial_runner_honours_caller_selected_profile_and_workload(
    tmp_path: Path,
) -> None:
    artifacts_dir = tmp_path / "artifacts"
    export_models(CONFIG_PATH, artifacts_dir, profiles=("balanced",))
    spec = TrialSpec(
        profile_name="balanced",
        batch_size=4,
        expert_0_fraction=0.0,
        warmup_requests=1,
        measured_requests=2,
        trial_index=2,
        seed=90210,
        measured_architectures=("C", "D"),
    )

    report = run_benchmark_trial(CONFIG_PATH, artifacts_dir, spec)

    assert report["mode"] == "trial"
    assert report["seed"] == 90210
    assert report["profile"] == "balanced"
    assert report["workload"] == {
        "batch_size": 4,
        "requested_expert_0_fraction": 0.0,
        "actual_role_counts": {"expert_0": 0, "expert_1": 4},
    }
    assert report["measurement"]["trial_index"] == 2
    assert report["measurement"]["correctness_oracle"] == "A"
    assert report["measurement"]["measured_architectures"] == ["C", "D"]
    assert report["measurement"]["warmup_requests_per_architecture"] == 1
    assert report["measurement"]["measured_requests_per_architecture"] == 2
    assert len(report["measurement"]["candidate_orders"]) == 2
    assert all(
        set(order) == {"C", "D"}
        for order in report["measurement"]["candidate_orders"]
    )
    assert report["correctness"]["passed"] is True
    assert set(report["correctness"]["first_runs"]) == {"A", "C", "D"}
    assert set(report["results"]) == {"C", "D"}
    for result in report["results"].values():
        assert result["summary"]["sample_count"] == 2


def test_trial_runner_uses_oracle_to_reject_a_measured_mismatch(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class FakeRuntime:
        def run(
            self,
            architecture: str,
            observations: np.ndarray,
            roles: tuple[str, ...],
        ) -> RunResult:
            del roles
            value = 1.0 if architecture == "D" else 0.0
            return RunResult(
                actions=np.full(
                    (len(observations), 2),
                    value,
                    dtype=np.float32,
                ),
                calls=(),
            )

    monkeypatch.setattr(
        benchmark.BenchmarkRuntime,
        "from_paths",
        lambda *args: FakeRuntime(),
    )
    spec = TrialSpec(
        profile_name="small",
        batch_size=4,
        expert_0_fraction=0.5,
        warmup_requests=1,
        measured_requests=2,
        trial_index=0,
        seed=301,
        measured_architectures=("C", "D"),
    )

    with pytest.raises(RuntimeError, match="architecture D"):
        run_benchmark_trial(CONFIG_PATH, tmp_path / "artifacts", spec)


def test_trial_runner_rejects_unknown_profile(tmp_path: Path) -> None:
    spec = TrialSpec(
        profile_name="unknown",
        batch_size=4,
        expert_0_fraction=0.5,
        warmup_requests=1,
        measured_requests=2,
        trial_index=0,
        seed=301,
    )

    with pytest.raises(ValueError, match="unknown benchmark profile"):
        run_benchmark_trial(CONFIG_PATH, tmp_path / "artifacts", spec)


def test_smoke_report_contains_raw_and_summary_evidence(
    tmp_path: Path,
) -> None:
    artifacts_dir = tmp_path / "artifacts"
    export_models(CONFIG_PATH, artifacts_dir, profiles=("small",))

    report = run_smoke_benchmark(CONFIG_PATH, artifacts_dir)

    assert report["mode"] == "smoke"
    assert report["profile"] == "small"
    assert report["workload"]["batch_size"] == 16
    assert report["workload"]["actual_role_counts"] == {
        "expert_0": 8,
        "expert_1": 8,
    }
    assert report["correctness"]["passed"] is True
    assert report["measurement"]["trials"] == 1
    assert len(report["measurement"]["candidate_orders"]) == 5
    assert set(report["results"]) == {"A", "B", "C", "D", "E"}

    for architecture, result in report["results"].items():
        assert len(result["request_samples_ns"]) == 5
        assert len(result["model_samples_ns"]) == 5
        assert len(result["call_samples"]) == 5
        assert result["call_count_per_request"] == len(
            result["call_samples"][0]
        )
        assert result["summary"]["sample_count"] == 5
        assert result["summary"]["p50_request_ns"] > 0
        assert result["summary"]["p50_model_ns"] >= 0
        for request_ns, model_ns, calls in zip(
            result["request_samples_ns"],
            result["model_samples_ns"],
            result["call_samples"],
            strict=True,
        ):
            assert model_ns == sum(call["duration_ns"] for call in calls)
            assert request_ns >= model_ns

    report_path = write_report(report, tmp_path / "runs" / "smoke.json")
    decoded = json.loads(report_path.read_text(encoding="utf-8"))
    assert decoded["correctness"]["passed"] is True
