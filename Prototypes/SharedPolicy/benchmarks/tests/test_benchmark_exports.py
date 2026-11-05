"""Correctness and contract tests for exported A–E models."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import pytest

from benchmarks.export_models import export_models


CONFIG_PATH = Path(__file__).parents[1] / "config.json"
EXPECTED_ARTIFACTS = {
    "A_all_heads",
    "B_masked",
    "C_expert_0",
    "C_expert_1",
    "D_conditional",
    "E_backbone",
    "E_expert_0_head",
    "E_expert_1_head",
}


@pytest.fixture(scope="module")
def exported_small_profile(
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[Path, dict[str, object]]:
    output_dir = tmp_path_factory.mktemp("benchmark-exports")
    manifest_path = export_models(
        config_path=CONFIG_PATH,
        output_dir=output_dir,
        profiles=("small",),
    )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    return output_dir / "small", manifest


def _session(profile_dir: Path, artifact_name: str) -> ort.InferenceSession:
    return ort.InferenceSession(
        str(profile_dir / f"{artifact_name}.onnx"),
        providers=["CPUExecutionProvider"],
    )


def test_exports_expected_separate_files_and_manifest(
    exported_small_profile: tuple[Path, dict[str, object]],
) -> None:
    profile_dir, manifest = exported_small_profile
    profile = manifest["profiles"]["small"]
    artifacts = profile["artifacts"]

    assert set(artifacts) == EXPECTED_ARTIFACTS
    assert {path.stem for path in profile_dir.glob("*.onnx")} == (
        EXPECTED_ARTIFACTS
    )
    assert all(artifact["bytes"] > 0 for artifact in artifacts.values())
    assert all(len(artifact["sha256"]) == 64 for artifact in artifacts.values())
    assert artifacts["D_conditional"]["inputs"] == [
        {
            "name": "observations",
            "dtype": "float32",
            "shape": ["batch_size", 64],
        },
        {"name": "use_first_expert", "dtype": "bool", "shape": []},
    ]
    assert all(
        tensor["dtype"] == "float32"
        for artifact in artifacts.values()
        for tensor in artifact["outputs"]
    )

    source_hashes = profile["source_parameter_hashes"]
    assert artifacts["A_all_heads"]["parameter_hashes"] == source_hashes
    assert artifacts["B_masked"]["parameter_hashes"] == source_hashes
    assert artifacts["D_conditional"]["parameter_hashes"] == source_hashes
    assert artifacts["C_expert_0"]["parameter_hashes"]["backbone"] == (
        source_hashes["backbone"]
    )
    assert artifacts["E_backbone"]["parameter_hashes"]["backbone"] == (
        source_hashes["backbone"]
    )


@pytest.mark.parametrize("batch_size", [1, 3])
def test_a_through_e_return_equivalent_actions(
    exported_small_profile: tuple[Path, dict[str, object]],
    batch_size: int,
) -> None:
    profile_dir, _ = exported_small_profile
    observations = np.random.default_rng(301).normal(
        size=(batch_size, 64),
    ).astype(np.float32)
    role_indices = np.arange(batch_size) % 2
    role_masks = np.eye(2, dtype=np.float32)[role_indices]

    all_actions = _session(profile_dir, "A_all_heads").run(
        None,
        {"observations": observations},
    )
    expected_selected = np.stack(all_actions, axis=1)[
        np.arange(batch_size),
        role_indices,
    ]
    masked_actions = _session(profile_dir, "B_masked").run(
        None,
        {"observations": observations, "role_masks": role_masks},
    )[0]
    np.testing.assert_allclose(
        masked_actions,
        expected_selected,
        rtol=1e-4,
        atol=1e-5,
    )

    conditional_session = _session(profile_dir, "D_conditional")
    for role_index, use_first_expert in enumerate((True, False)):
        conditional_actions = conditional_session.run(
            None,
            {
                "observations": observations,
                "use_first_expert": np.array(use_first_expert, dtype=np.bool_),
            },
        )[0]
        np.testing.assert_allclose(
            conditional_actions,
            all_actions[role_index],
            rtol=1e-4,
            atol=1e-5,
        )

    features = _session(profile_dir, "E_backbone").run(
        None,
        {"observations": observations},
    )[0]
    for role_index, role in enumerate(("expert_0", "expert_1")):
        complete_actions = _session(profile_dir, f"C_{role}").run(
            None,
            {"observations": observations},
        )[0]
        head_actions = _session(profile_dir, f"E_{role}_head").run(
            None,
            {"features": features},
        )[0]
        np.testing.assert_allclose(
            complete_actions,
            all_actions[role_index],
            rtol=1e-4,
            atol=1e-5,
        )
        np.testing.assert_allclose(
            head_actions,
            all_actions[role_index],
            rtol=1e-4,
            atol=1e-5,
        )


def test_rejects_an_explicitly_empty_profile_selection(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="at least one profile"):
        export_models(CONFIG_PATH, tmp_path, profiles=())


def test_every_export_has_a_dynamic_batch_dimension(
    exported_small_profile: tuple[Path, dict[str, object]],
) -> None:
    profile_dir, _ = exported_small_profile

    for artifact_name in EXPECTED_ARTIFACTS:
        model = onnx.load(profile_dir / f"{artifact_name}.onnx")
        onnx.checker.check_model(model)
        public_tensors = (
            value
            for value in (*model.graph.input, *model.graph.output)
            if value.type.tensor_type.shape.dim
        )
        assert all(
            value.type.tensor_type.shape.dim[0].dim_param == "batch_size"
            for value in public_tensors
        )


def test_b_keeps_runtime_mask_and_both_head_computations(
    exported_small_profile: tuple[Path, dict[str, object]],
) -> None:
    profile_dir, _ = exported_small_profile
    model = onnx.load(profile_dir / "B_masked.onnx")
    input_names = [value.name for value in model.graph.input]
    operation_types = [node.op_type for node in model.graph.node]

    assert input_names == ["observations", "role_masks"]
    assert "Mul" in operation_types
    assert "ReduceSum" in operation_types
    # Two backbone Linear layers plus two Linear layers in each of two heads.
    assert operation_types.count("Gemm") == 6


def test_d_contains_one_if_with_two_head_branches(
    exported_small_profile: tuple[Path, dict[str, object]],
) -> None:
    profile_dir, _ = exported_small_profile
    model = onnx.load(profile_dir / "D_conditional.onnx")
    if_nodes = [node for node in model.graph.node if node.op_type == "If"]

    assert len(if_nodes) == 1
    branch_graphs = [
        attribute.g
        for attribute in if_nodes[0].attribute
        if attribute.name in {"then_branch", "else_branch"}
    ]
    assert len(branch_graphs) == 2
    assert all(
        [node.op_type for node in branch.node].count("Gemm") == 2
        for branch in branch_graphs
    )
