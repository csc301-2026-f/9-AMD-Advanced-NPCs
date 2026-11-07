"""Collect and validate ONNX Runtime operator evidence for A, B, and D."""

from __future__ import annotations

import json
import platform
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import ModelProto, NodeProto

from benchmarks.models import load_benchmark_config
from benchmarks.runtime import create_session_options, load_runtime_settings


@dataclass(frozen=True)
class ProfileCase:
    """One isolated model invocation whose executed operators we inspect."""

    name: str
    artifact: str
    selected_expert: str | None


PROFILE_CASES = (
    ProfileCase("A_all_heads", "A_all_heads", None),
    ProfileCase("B_select_expert_0", "B_masked", "expert_0"),
    ProfileCase("B_select_expert_1", "B_masked", "expert_1"),
    ProfileCase("D_select_expert_0", "D_conditional", "expert_0"),
    ProfileCase("D_select_expert_1", "D_conditional", "expert_1"),
)


def _assign_nodes(
    mapping: dict[str, str],
    nodes: list[NodeProto],
    stage: str,
) -> None:
    for node in nodes:
        if not node.name:
            raise ValueError("profiled ONNX nodes must have names")
        if node.name in mapping:
            raise ValueError(f"duplicate ONNX node name {node.name!r}")
        mapping[node.name] = stage


def _expect_operations(nodes: list[NodeProto], expected: list[str]) -> None:
    actual = [node.op_type for node in nodes]
    if actual != expected:
        raise ValueError(
            f"unexpected exported graph operations: expected {expected}, got {actual}"
        )


def build_stage_map(model: ModelProto, roles: tuple[str, ...]) -> dict[str, str]:
    """Map exported node names to semantic model stages before ORT fusion."""

    nodes = list(model.graph.node)
    backbone_nodes = nodes[:4]
    _expect_operations(backbone_nodes, ["Gemm", "Relu", "Gemm", "Relu"])
    mapping: dict[str, str] = {}
    _assign_nodes(mapping, backbone_nodes, "backbone")

    if_nodes = [node for node in nodes if node.op_type == "If"]
    if if_nodes:
        if len(if_nodes) != 1 or nodes[4] is not if_nodes[0]:
            raise ValueError("conditional graph must contain one If after the backbone")
        if_node = if_nodes[0]
        _assign_nodes(mapping, [if_node], "control_flow")
        branches = {attribute.name: list(attribute.g.node) for attribute in if_node.attribute}
        branch_roles = {"then_branch": roles[0], "else_branch": roles[1]}
        for branch_name, role in branch_roles.items():
            branch_nodes = branches.get(branch_name)
            if branch_nodes is None:
                raise ValueError(f"conditional graph is missing {branch_name}")
            _expect_operations(branch_nodes, ["Gemm", "Relu", "Gemm"])
            _assign_nodes(mapping, branch_nodes, f"{role}_head")
        return mapping

    offset = 4
    for role in roles:
        head_nodes = nodes[offset : offset + 3]
        _expect_operations(head_nodes, ["Gemm", "Relu", "Gemm"])
        _assign_nodes(mapping, head_nodes, f"{role}_head")
        offset += 3
    selection_nodes = nodes[offset:]
    input_names = {value.name for value in model.graph.input}
    if "role_masks" in input_names:
        if not selection_nodes:
            raise ValueError("masked graph must contain selection operations")
        _assign_nodes(mapping, selection_nodes, "selection")
    elif selection_nodes:
        raise ValueError("all-head graph contains unexpected trailing operations")
    return mapping


def _profile_node_name(event_name: str) -> str:
    name = event_name.removesuffix("_kernel_time")
    return name.removeprefix("fused ")


def parse_profile_events(
    profile_path: str | Path,
    stage_map: dict[str, str],
) -> list[dict[str, object]]:
    """Reduce ORT's trace to mapped operator-execution events."""

    raw_events = json.loads(Path(profile_path).read_text(encoding="utf-8"))
    events: list[dict[str, object]] = []
    for raw in raw_events:
        if raw.get("cat") != "Node" or not raw.get("name", "").endswith(
            "_kernel_time"
        ):
            continue
        node_name = _profile_node_name(raw["name"])
        try:
            stage = stage_map[node_name]
        except KeyError as error:
            raise ValueError(
                f"profile event {raw['name']!r} does not map to an exported node"
            ) from error
        arguments = raw.get("args", {})
        events.append(
            {
                "node": node_name,
                "operator": arguments.get("op_name", "unknown"),
                "provider": arguments.get("provider", "unknown"),
                "stage": stage,
                "duration_us": raw.get("dur", 0),
            }
        )
    if not events:
        raise ValueError(f"no operator events found in profile {profile_path}")
    return events


def _inputs_for_case(
    case: ProfileCase,
    observations: np.ndarray,
    roles: tuple[str, ...],
) -> dict[str, np.ndarray]:
    if case.artifact == "A_all_heads":
        return {"observations": observations}
    if case.artifact == "B_masked":
        selected_index = roles.index(case.selected_expert)
        role_masks = np.zeros((len(observations), len(roles)), dtype=np.float32)
        role_masks[:, selected_index] = 1.0
        return {"observations": observations, "role_masks": role_masks}
    return {
        "observations": observations,
        "use_first_expert": np.array(
            case.selected_expert == roles[0],
            dtype=np.bool_,
        ),
    }


def _summarize_stages(events: list[dict[str, object]]) -> dict[str, object]:
    counts: defaultdict[str, int] = defaultdict(int)
    durations: defaultdict[str, int] = defaultdict(int)
    for event in events:
        stage = str(event["stage"])
        counts[stage] += 1
        durations[stage] += int(event["duration_us"])
    return {
        stage: {
            "operator_events": counts[stage],
            "profile_duration_us": durations[stage],
        }
        for stage in sorted(counts)
    }


def _validate_executed_stages(
    case: ProfileCase,
    executed_stages: set[str],
    roles: tuple[str, ...],
) -> None:
    head_stages = {f"{role}_head" for role in roles}
    if case.artifact in {"A_all_heads", "B_masked"}:
        missing = {"backbone", *head_stages} - executed_stages
        if missing:
            raise RuntimeError(f"{case.name} did not execute expected stages: {missing}")
        return

    expected_head = f"{case.selected_expert}_head"
    unexpected_heads = head_stages - {expected_head}
    if expected_head not in executed_stages or executed_stages & unexpected_heads:
        raise RuntimeError(
            f"{case.name} conditional branch evidence is inconsistent: "
            f"executed {sorted(executed_stages)}"
        )


def run_operator_profiles(
    config_path: str | Path,
    artifacts_dir: str | Path,
    profile_name: str = "small",
    batch_size: int = 16,
) -> dict[str, object]:
    """Profile isolated A/B/D cases and validate their executed stages."""

    config = load_benchmark_config(config_path)
    settings = load_runtime_settings(config_path)
    if batch_size <= 0:
        raise ValueError("batch_size must be positive")
    dimensions = config.dimensions(profile_name)
    observations = np.random.default_rng(config.seed).normal(
        size=(batch_size, dimensions.observation)
    ).astype(np.float32)
    profile_dir = Path(artifacts_dir) / profile_name
    manifest = json.loads(
        (Path(artifacts_dir) / "manifest.json").read_text(encoding="utf-8")
    )
    artifact_manifest = manifest["profiles"][profile_name]["artifacts"]
    cases: dict[str, object] = {}

    with tempfile.TemporaryDirectory(prefix="shared-policy-profiles-") as temporary:
        temporary_dir = Path(temporary)
        for case in PROFILE_CASES:
            model_path = profile_dir / f"{case.artifact}.onnx"
            model = onnx.load(model_path)
            stage_map = build_stage_map(model, config.roles)
            options = create_session_options(
                settings,
                temporary_dir / case.name,
            )
            session = ort.InferenceSession(
                str(model_path),
                sess_options=options,
                providers=[settings.provider],
            )
            session.run(
                None,
                _inputs_for_case(case, observations, config.roles),
            )
            raw_profile_path = session.end_profiling()
            events = parse_profile_events(raw_profile_path, stage_map)
            stage_summary = _summarize_stages(events)
            executed_stages = set(stage_summary)
            _validate_executed_stages(case, executed_stages, config.roles)
            cases[case.name] = {
                "artifact": case.artifact,
                "selected_expert": case.selected_expert,
                "executed_stages": sorted(executed_stages),
                "stage_summary": stage_summary,
                "operator_events": events,
            }

    return {
        "mode": "operator_profile",
        "seed": config.seed,
        "profile": profile_name,
        "batch_size": batch_size,
        "runtime": {
            "provider": settings.provider,
            "execution_mode": settings.execution_mode,
            "intra_op_threads": settings.intra_op_threads,
            "inter_op_threads": settings.inter_op_threads,
            "graph_optimization": settings.graph_optimization,
        },
        "environment": {
            "python": platform.python_version(),
            "platform": platform.platform(),
            "machine": platform.machine(),
            "numpy": np.__version__,
            "onnx": onnx.__version__,
            "onnxruntime": ort.__version__,
        },
        "artifacts": {
            name: {
                "file": artifact_manifest[name]["file"],
                "sha256": artifact_manifest[name]["sha256"],
            }
            for name in ("A_all_heads", "B_masked", "D_conditional")
        },
        "cases": cases,
        "validation": {
            "passed": True,
            "meaning": (
                "A and B execute both heads; each D invocation executes only "
                "the selected If branch"
            ),
            "duration_note": (
                "Profile event durations are diagnostic and are not benchmark "
                "latency measurements"
            ),
        },
    }
