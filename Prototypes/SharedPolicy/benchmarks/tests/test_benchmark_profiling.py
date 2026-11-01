"""Tests for semantic mapping and validation of ONNX Runtime profiles."""

from __future__ import annotations

import json
from pathlib import Path

import onnx
import pytest

from benchmarks.export_models import export_models
from benchmarks.profiling import (
    build_stage_map,
    parse_profile_events,
    run_operator_profiles,
)


CONFIG_PATH = Path(__file__).parents[1] / "config.json"


@pytest.fixture(scope="module")
def exported_artifacts(tmp_path_factory: pytest.TempPathFactory) -> Path:
    output_dir = tmp_path_factory.mktemp("profile-exports")
    export_models(CONFIG_PATH, output_dir, profiles=("small",))
    return output_dir


def test_maps_exported_nodes_to_semantic_stages(
    exported_artifacts: Path,
) -> None:
    profile_dir = exported_artifacts / "small"
    roles = ("expert_0", "expert_1")

    for artifact in ("A_all_heads", "B_masked", "D_conditional"):
        model = onnx.load(profile_dir / f"{artifact}.onnx")
        stage_map = build_stage_map(model, roles)

        assert "backbone" in stage_map.values()
        assert "expert_0_head" in stage_map.values()
        assert "expert_1_head" in stage_map.values()
        expected_node_names = {node.name for node in model.graph.node}
        expected_node_names.update(
            branch_node.name
            for node in model.graph.node
            if node.op_type == "If"
            for attribute in node.attribute
            if attribute.name in {"then_branch", "else_branch"}
            for branch_node in attribute.g.node
        )
        assert set(stage_map) == expected_node_names


def test_rejects_unmapped_profile_events(tmp_path: Path) -> None:
    profile_path = tmp_path / "profile.json"
    profile_path.write_text(
        json.dumps(
            [
                {
                    "cat": "Node",
                    "name": "unknown_kernel_time",
                    "dur": 1,
                    "args": {"op_name": "Gemm"},
                }
            ]
        ),
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="does not map"):
        parse_profile_events(profile_path, {"known": "backbone"})


def test_profiles_prove_masked_and_conditional_execution(
    exported_artifacts: Path,
) -> None:
    report = run_operator_profiles(CONFIG_PATH, exported_artifacts)

    assert report["validation"]["passed"] is True
    assert report["environment"]["onnxruntime"]
    assert set(report["artifacts"]) == {
        "A_all_heads",
        "B_masked",
        "D_conditional",
    }
    assert all(
        len(artifact["sha256"]) == 64
        for artifact in report["artifacts"].values()
    )
    cases = report["cases"]
    both_heads = {"backbone", "expert_0_head", "expert_1_head"}
    assert both_heads <= set(cases["A_all_heads"]["executed_stages"])
    assert both_heads <= set(cases["B_select_expert_0"]["executed_stages"])
    assert both_heads <= set(cases["B_select_expert_1"]["executed_stages"])

    first_stages = set(cases["D_select_expert_0"]["executed_stages"])
    second_stages = set(cases["D_select_expert_1"]["executed_stages"])
    assert "expert_0_head" in first_stages
    assert "expert_1_head" not in first_stages
    assert "expert_1_head" in second_stages
    assert "expert_0_head" not in second_stages

    for case in cases.values():
        assert case["operator_events"]
        assert all(
            event["stage"] != "unmapped"
            for event in case["operator_events"]
        )
