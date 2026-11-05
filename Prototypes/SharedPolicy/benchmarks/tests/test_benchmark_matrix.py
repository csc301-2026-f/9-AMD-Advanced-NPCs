"""Tests for fresh-process benchmark matrix planning and execution."""

from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path

import pytest

from benchmarks.benchmark import TrialSpec
from benchmarks.export_models import export_models
from benchmarks.matrix import (
    build_matrix_specs,
    run_benchmark_matrix,
    validate_matrix_trials,
)


CONFIG_PATH = Path(__file__).parents[1] / "config.json"


def _write_reduced_config(tmp_path: Path) -> Path:
    raw = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    reduced = deepcopy(raw)
    reduced["profiles"] = {"small": reduced["profiles"]["small"]}
    reduced["workloads"]["batch_sizes"] = [4]
    reduced["workloads"]["expert_0_fractions"] = [0.5]
    reduced["measurement"] = {
        "warmup_requests": 1,
        "measured_requests": 2,
        "trials": 2,
    }
    config_path = tmp_path / "matrix-config.json"
    config_path.write_text(json.dumps(reduced), encoding="utf-8")
    return config_path


def _minimal_report(
    spec: TrialSpec,
    started_ns: int,
    finished_ns: int,
) -> dict[str, object]:
    return {
        "profile": spec.profile_name,
        "seed": spec.seed,
        "workload": {
            "batch_size": spec.batch_size,
            "requested_expert_0_fraction": spec.expert_0_fraction,
        },
        "measurement": {"trial_index": spec.trial_index},
        "correctness": {"passed": True},
        "results": {architecture: {} for architecture in spec.measured_architectures},
        "worker": {
            "pid": spec.seed,
            "started_wall_time_ns": started_ns,
            "finished_wall_time_ns": finished_ns,
        },
    }


def test_default_matrix_plan_has_144_unique_seeded_trials() -> None:
    specs = build_matrix_specs(CONFIG_PATH, ("C", "D"))

    assert len(specs) == 3 * 4 * 4 * 3 == 144
    assert len({spec.seed for spec in specs}) == 144
    assert {
        (
            spec.profile_name,
            spec.batch_size,
            spec.expert_0_fraction,
            spec.trial_index,
        )
        for spec in specs
    } == {
        (profile, batch_size, fraction, trial_index)
        for profile in ("small", "balanced", "expensive_heads")
        for batch_size in (1, 16, 64, 256)
        for fraction in (1.0, 0.5, 0.1, 0.0)
        for trial_index in range(3)
    }
    assert all(spec.correctness_oracle == "A" for spec in specs)
    assert all(spec.measured_architectures == ("C", "D") for spec in specs)


def test_matrix_validation_rejects_duplicate_and_missing_reports() -> None:
    specs = build_matrix_specs(CONFIG_PATH, ("C", "D"))[:2]
    first = _minimal_report(specs[0], 100, 200)
    second = _minimal_report(specs[1], 200, 300)

    validate_matrix_trials(specs, [first, second])
    with pytest.raises(ValueError, match="duplicate trial identities"):
        validate_matrix_trials(specs, [first, first])
    with pytest.raises(ValueError, match="do not match planned trials"):
        validate_matrix_trials(specs, [first])


def test_matrix_validation_rejects_overlapping_workers() -> None:
    specs = build_matrix_specs(CONFIG_PATH, ("C", "D"))[:2]
    reports = [
        _minimal_report(specs[0], 100, 250),
        _minimal_report(specs[1], 200, 300),
    ]

    with pytest.raises(ValueError, match="intervals overlap"):
        validate_matrix_trials(specs, reports)


def test_reduced_matrix_runs_in_fresh_sequential_workers(tmp_path: Path) -> None:
    config_path = _write_reduced_config(tmp_path)
    artifacts_dir = tmp_path / "artifacts"
    export_models(config_path, artifacts_dir)

    report = run_benchmark_matrix(config_path, artifacts_dir, ("C", "D"))

    assert report["mode"] == "matrix"
    assert report["correctness_oracle"] == "A"
    assert report["measured_architectures"] == ["C", "D"]
    assert report["matrix"] == {
        "profiles": ["small"],
        "batch_sizes": [4],
        "expert_0_fractions": [0.5],
        "trials_per_case": 2,
        "trial_count": 2,
        "fresh_process_per_trial": True,
        "workers_sequential": True,
    }
    assert report["validation"] == {
        "passed": True,
        "completed_trials": 2,
        "correctness_failures": 0,
        "overlapping_workers": 0,
    }
    assert len(report["trials"]) == 2
    assert len(report["case_index"]) == 2
    assert all(
        set(trial["results"]) == {"C", "D"}
        for trial in report["trials"]
    )
    assert all(
        set(trial["correctness"]["first_runs"]) == {"A", "C", "D"}
        for trial in report["trials"]
    )
    first_worker = report["trials"][0]["worker"]
    second_worker = report["trials"][1]["worker"]
    assert first_worker["pid"] != second_worker["pid"]
    assert (
        first_worker["finished_wall_time_ns"]
        <= second_worker["started_wall_time_ns"]
    )


def test_matrix_stops_when_a_worker_fails(tmp_path: Path) -> None:
    config_path = _write_reduced_config(tmp_path)

    with pytest.raises(RuntimeError, match="matrix worker 0 failed"):
        run_benchmark_matrix(
            config_path,
            tmp_path / "missing-artifacts",
            ("C", "D"),
        )
