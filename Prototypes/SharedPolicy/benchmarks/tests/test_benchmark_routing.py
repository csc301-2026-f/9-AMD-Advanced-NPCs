"""Routing and failure tests for the shared A–E runtime contract."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

import numpy as np
import pytest

from benchmarks.export_models import export_models
from benchmarks.runtime import BenchmarkRuntime, FloatArray


CONFIG_PATH = Path(__file__).parents[1] / "config.json"
ARCHITECTURES = ("A", "B", "C", "D", "E")


@pytest.fixture(scope="module")
def exported_artifacts(tmp_path_factory: pytest.TempPathFactory) -> Path:
    output_dir = tmp_path_factory.mktemp("routing-artifacts")
    export_models(CONFIG_PATH, output_dir, profiles=("small",))
    return output_dir


@pytest.fixture
def runtime(exported_artifacts: Path) -> BenchmarkRuntime:
    return BenchmarkRuntime.from_paths(
        CONFIG_PATH,
        exported_artifacts,
        "small",
    )


def _observations(batch_size: int) -> FloatArray:
    return np.random.default_rng(301).normal(
        size=(batch_size, 64),
    ).astype(np.float32)


@pytest.mark.parametrize(
    "roles",
    [
        ("expert_1", "expert_0", "expert_1"),
        ("expert_1",),
        ("expert_0",) * 9 + ("expert_1",),
    ],
    ids=["shuffled-mixed", "singleton", "ninety-ten"],
)
def test_all_architectures_restore_equivalent_actions(
    runtime: BenchmarkRuntime,
    roles: tuple[str, ...],
) -> None:
    observations = _observations(len(roles))
    expected = runtime.run("A", observations, roles).actions

    for architecture in ARCHITECTURES:
        result = runtime.run(architecture, observations, roles)
        np.testing.assert_allclose(
            result.actions,
            expected,
            rtol=1e-4,
            atol=1e-5,
        )
        assert result.actions.shape == (len(roles), 2)
        assert result.actions.dtype == np.float32


def test_mixed_roles_have_expected_model_calls(
    runtime: BenchmarkRuntime,
) -> None:
    observations = _observations(3)
    roles = ("expert_1", "expert_0", "expert_1")
    expected_calls = {
        "A": (("A_all_heads", 3),),
        "B": (("B_masked", 3),),
        "C": (("C_expert_0", 1), ("C_expert_1", 2)),
        "D": (("D_conditional", 1), ("D_conditional", 2)),
        "E": (
            ("E_backbone", 3),
            ("E_expert_0_head", 1),
            ("E_expert_1_head", 2),
        ),
    }

    for architecture, expected in expected_calls.items():
        result = runtime.run(architecture, observations, roles)
        actual = tuple((call.artifact, call.rows) for call in result.calls)
        assert actual == expected
        assert result.call_count == len(expected)


def test_homogeneous_request_skips_empty_role_groups(
    runtime: BenchmarkRuntime,
) -> None:
    observations = _observations(4)
    roles = ("expert_1",) * 4

    assert [
        call.artifact for call in runtime.run("C", observations, roles).calls
    ] == ["C_expert_1"]
    assert [
        call.artifact for call in runtime.run("D", observations, roles).calls
    ] == ["D_conditional"]
    assert [
        call.artifact for call in runtime.run("E", observations, roles).calls
    ] == ["E_backbone", "E_expert_1_head"]


@pytest.mark.parametrize(
    ("architecture", "observations", "roles", "error", "message"),
    [
        (
            "missing",
            np.zeros((1, 64), dtype=np.float32),
            ("expert_0",),
            ValueError,
            "unknown architecture",
        ),
        (
            "A",
            np.zeros((0, 64), dtype=np.float32),
            (),
            ValueError,
            "at least one row",
        ),
        (
            "A",
            np.zeros((1, 63), dtype=np.float32),
            ("expert_0",),
            ValueError,
            "wrong feature width",
        ),
        (
            "A",
            np.zeros((64,), dtype=np.float32),
            ("expert_0",),
            ValueError,
            "shape",
        ),
        (
            "A",
            np.zeros((1, 64), dtype=np.float64),
            ("expert_0",),
            TypeError,
            "float32",
        ),
        (
            "A",
            np.zeros((2, 64), dtype=np.float32),
            ("expert_0",),
            ValueError,
            "one entry",
        ),
        (
            "A",
            np.zeros((1, 64), dtype=np.float32),
            ("stealth",),
            ValueError,
            "unknown roles",
        ),
    ],
)
def test_rejects_invalid_requests_before_returning_actions(
    runtime: BenchmarkRuntime,
    architecture: str,
    observations: FloatArray,
    roles: tuple[str, ...],
    error: type[Exception],
    message: str,
) -> None:
    with pytest.raises(error, match=message):
        runtime.run(architecture, observations, roles)


def test_rejects_a_missing_artifact(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError, match="A_all_heads.onnx"):
        BenchmarkRuntime.from_paths(CONFIG_PATH, tmp_path, "small")


class FailingSession:
    """Replacement session proving failed requests return no partial result."""

    def run(
        self,
        output_names: Sequence[str] | None,
        input_feed: dict[str, np.ndarray],
    ) -> list[FloatArray]:
        raise RuntimeError("deliberate session failure")


def test_model_failure_is_reported_without_a_partial_result(
    exported_artifacts: Path,
) -> None:
    runtime = BenchmarkRuntime.from_paths(
        CONFIG_PATH,
        exported_artifacts,
        "small",
    )
    runtime._sessions["C_expert_1"] = FailingSession()  # type: ignore[assignment]

    with pytest.raises(RuntimeError, match="C_expert_1 inference failed"):
        runtime.run(
            "C",
            _observations(2),
            ("expert_0", "expert_1"),
        )
