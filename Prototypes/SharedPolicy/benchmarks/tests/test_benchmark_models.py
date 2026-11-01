"""Tests for configurable seeded shared-policy benchmark components."""

from pathlib import Path

import pytest
import torch

from benchmarks.models import (
    BenchmarkConfig,
    PolicyDimensions,
    ProfileWidths,
    build_seeded_policy,
    load_benchmark_config,
)


CONFIG_PATH = Path(__file__).parents[1] / "config.json"


def test_loads_expected_benchmark_model_contract() -> None:
    config = load_benchmark_config(CONFIG_PATH)

    assert config.seed == 301
    assert config.observation_size == 64
    assert config.action_size == 2
    assert config.roles == ("combat", "support")
    assert config.profiles["small"] == ProfileWidths(backbone=128, head=32)
    assert config.profiles["balanced"] == ProfileWidths(
        backbone=256,
        head=256,
    )
    assert config.profiles["expensive_heads"] == ProfileWidths(
        backbone=256,
        head=1024,
    )


@pytest.mark.parametrize("batch_size", [1, 3])
def test_seeded_policy_returns_ordered_float32_actions(
    batch_size: int,
) -> None:
    config = load_benchmark_config(CONFIG_PATH)
    model = build_seeded_policy(config, "small")
    observations = torch.zeros(
        (batch_size, config.observation_size),
        dtype=torch.float32,
    )

    outputs = model(observations)

    assert model.roles == ("combat", "support")
    assert len(outputs) == len(config.roles)
    assert all(
        output.shape == (batch_size, config.action_size) for output in outputs
    )
    assert all(output.dtype == torch.float32 for output in outputs)
    assert all(not parameter.requires_grad for parameter in model.parameters())


def test_same_seed_reproduces_every_parameter() -> None:
    config = load_benchmark_config(CONFIG_PATH)
    first = build_seeded_policy(config, "balanced")
    second = build_seeded_policy(config, "balanced")

    assert first.state_dict().keys() == second.state_dict().keys()
    for name, first_parameter in first.state_dict().items():
        torch.testing.assert_close(first_parameter, second.state_dict()[name])


def test_seeded_builder_does_not_change_global_torch_rng() -> None:
    config = load_benchmark_config(CONFIG_PATH)
    torch.manual_seed(7)
    expected = torch.rand(4)

    torch.manual_seed(7)
    build_seeded_policy(config, "small")
    actual = torch.rand(4)

    torch.testing.assert_close(actual, expected)


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("observation", 0, "observation dimension"),
        ("backbone", -1, "backbone dimension"),
        ("head", True, "head dimension"),
        ("action", 0, "action dimension"),
    ],
)
def test_rejects_invalid_model_dimensions(
    field: str,
    value: object,
    message: str,
) -> None:
    values: dict[str, object] = {
        "observation": 4,
        "backbone": 8,
        "head": 4,
        "action": 2,
        "roles": ("combat", "support"),
    }
    values[field] = value

    with pytest.raises(ValueError, match=message):
        PolicyDimensions(**values)  # type: ignore[arg-type]


@pytest.mark.parametrize(
    ("roles", "message"),
    [
        ((), "at least one"),
        (("combat", "combat"), "unique"),
        (("combat", "support-role"), "identifiers"),
    ],
)
def test_rejects_invalid_roles(
    roles: tuple[str, ...],
    message: str,
) -> None:
    with pytest.raises(ValueError, match=message):
        PolicyDimensions(
            observation=4,
            backbone=8,
            head=4,
            action=2,
            roles=roles,
        )


def test_rejects_unknown_profile() -> None:
    config = load_benchmark_config(CONFIG_PATH)

    with pytest.raises(ValueError, match="unknown profile 'missing'"):
        build_seeded_policy(config, "missing")


@pytest.mark.parametrize(
    ("observations", "error", "message"),
    [
        (
            torch.zeros(64, dtype=torch.float32),
            ValueError,
            "rank 2",
        ),
        (
            torch.zeros((2, 63), dtype=torch.float32),
            ValueError,
            "wrong feature width",
        ),
        (
            torch.zeros((2, 64), dtype=torch.float64),
            TypeError,
            "torch.float32",
        ),
    ],
)
def test_rejects_invalid_observation_tensors(
    observations: torch.Tensor,
    error: type[Exception],
    message: str,
) -> None:
    config = load_benchmark_config(CONFIG_PATH)
    model = build_seeded_policy(config, "small")

    with pytest.raises(error, match=message):
        model(observations)


def test_benchmark_config_rejects_duplicate_roles() -> None:
    with pytest.raises(ValueError, match="roles must be unique"):
        BenchmarkConfig(
            seed=301,
            observation_size=64,
            action_size=2,
            roles=("combat", "combat"),
            profiles={"small": ProfileWidths(backbone=8, head=4)},
        )
