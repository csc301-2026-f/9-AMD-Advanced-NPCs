#!/usr/bin/env python3
"""Aggregate isolated Unreal C/D workload reports without external packages."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any


def load_report(path: Path) -> tuple[tuple[int, int, int], dict[str, Any]]:
    with path.open(encoding="utf-8") as handle:
        report = json.load(handle)
    if report.get("mode") != "isolated_workload":
        raise ValueError(f"{path}: expected isolated_workload mode")
    if report.get("all_correct") is not True:
        raise ValueError(f"{path}: correctness gate did not pass")
    workloads = report.get("workloads")
    if not isinstance(workloads, list) or len(workloads) != 1:
        raise ValueError(f"{path}: expected exactly one workload")

    workload = workloads[0]
    batch = workload.get("batch_size")
    expert0_percent = workload.get("requested_expert_0_percent")
    trial = report.get("trial_index")
    if not all(type(value) is int for value in (batch, expert0_percent, trial)):
        raise ValueError(f"{path}: batch, expert share, and trial must be integers")
    expected_order = "C,D" if trial % 2 == 0 else "D,C"
    if workload.get("execution_order") != expected_order:
        raise ValueError(f"{path}: expected execution order {expected_order} for trial {trial}")

    for architecture in ("C", "D"):
        result = workload.get(architecture)
        if not isinstance(result, dict) or result.get("correct") is not True:
            raise ValueError(f"{path}: {architecture} result is missing or incorrect")
        samples = result.get("request_ms_raw")
        if not isinstance(samples, list) or len(samples) != report.get("measured_requests"):
            raise ValueError(f"{path}: {architecture} raw sample count mismatch")
        if any(not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 for value in samples):
            raise ValueError(f"{path}: {architecture} contains an invalid duration")
        for percentile in ("request_ms_p50", "request_ms_p95"):
            value = result.get(percentile)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                raise ValueError(f"{path}: {architecture} contains an invalid {percentile}")
    if workload["C"].get("calls") != workload["D"].get("calls"):
        raise ValueError(f"{path}: C/D model-call evidence differs")
    for field in ("expert_0_rows", "expert_1_rows"):
        if workload["C"].get(field) != workload["D"].get(field):
            raise ValueError(f"{path}: C/D {field} evidence differs")
    return (batch, expert0_percent, trial), report


def architecture_summary(trial_results: list[dict[str, Any]], architecture: str) -> dict[str, Any]:
    trial_p50 = [result[architecture]["request_ms_p50"] for result in trial_results]
    return {
        "trial_p50_ms": trial_p50,
        "median_trial_p50_ms": statistics.median(trial_p50),
        "min_trial_p50_ms": min(trial_p50),
        "max_trial_p50_ms": max(trial_p50),
        "population_stdev_trial_p50_ms": statistics.pstdev(trial_p50),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    seen: set[tuple[int, int, int]] = set()
    grouped: dict[tuple[int, int], list[tuple[int, dict[str, Any]]]] = defaultdict(list)
    metadata: dict[str, Any] | None = None
    for path in args.reports:
        key, report = load_report(path)
        if key in seen:
            raise ValueError(f"duplicate report for batch={key[0]} expert_0={key[1]} trial={key[2]}")
        seen.add(key)
        workload = report["workloads"][0]
        grouped[(key[0], key[1])].append((key[2], workload))
        current_metadata = {
            "engine_version": report.get("engine_version"),
            "platform": report.get("platform"),
            "os_version": report.get("os_version"),
            "runtime": report.get("runtime"),
            "warmup_requests": report.get("warmup_requests"),
            "measured_requests": report.get("measured_requests"),
        }
        if metadata is None:
            metadata = current_metadata
        elif current_metadata != metadata:
            raise ValueError(f"{path}: environment or measurement configuration differs")

    summaries = []
    for (batch, expert0_percent), indexed_results in sorted(grouped.items()):
        indexed_results.sort(key=lambda item: item[0])
        trials = [item[0] for item in indexed_results]
        results = [item[1] for item in indexed_results]
        evidence = {
            (
                result["C"]["expert_0_rows"],
                result["C"]["expert_1_rows"],
                json.dumps(result["C"]["calls"], sort_keys=True),
            )
            for result in results
        }
        if len(evidence) != 1:
            raise ValueError(
                f"batch={batch} expert_0={expert0_percent}: routing evidence differs across trials"
            )
        summaries.append(
            {
                "batch_size": batch,
                "requested_expert_0_percent": expert0_percent,
                "trial_indices": trials,
                "trial_count": len(trials),
                "expert_0_rows": results[0]["C"]["expert_0_rows"],
                "expert_1_rows": results[0]["C"]["expert_1_rows"],
                "calls": results[0]["C"]["calls"],
                "C": architecture_summary(results, "C"),
                "D": architecture_summary(results, "D"),
            }
        )

    output = {
        **(metadata or {}),
        "report_count": len(args.reports),
        "workload_count": len(summaries),
        "all_correct": True,
        "interpretation": (
            "Cross-process variation summary. Performance conclusions still require enough trials "
            "for stable differences and must not generalize beyond this environment."
        ),
        "workloads": summaries,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
