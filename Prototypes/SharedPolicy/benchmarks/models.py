"""Configurable seeded model components for architecture benchmarks."""

from __future__ import annotations

import json
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import torch
from torch import Tensor, nn


DEFAULT_CONFIG_PATH = Path(__file__).with_name("config.json")


def _require_positive_int(name: str, value: object) -> int:
    """Return a positive integer or reject the configuration clearly."""

    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{name} must be a positive integer; received {value!r}")
    return value


def _validate_roles(roles: tuple[str, ...]) -> None:
    """Require stable names that are safe for modules and tensor outputs."""

    if not roles:
        raise ValueError("roles must contain at least one role")
    if len(set(roles)) != len(roles):
        raise ValueError("roles must be unique")
    invalid_roles = [role for role in roles if not role.isidentifier()]
    if invalid_roles:
        raise ValueError(
            "roles must be non-empty Python identifiers; "
            f"received {invalid_roles}"
        )


@dataclass(frozen=True)
class ProfileWidths:
    """Hidden-layer widths that vary between benchmark profiles."""

    backbone: int
    head: int

    def __post_init__(self) -> None:
        _require_positive_int("backbone width", self.backbone)
        _require_positive_int("head width", self.head)


@dataclass(frozen=True)
class BenchmarkConfig:
    """Model contract shared by every benchmark architecture."""

    seed: int
    observation_size: int
    action_size: int
    roles: tuple[str, ...]
    profiles: dict[str, ProfileWidths]

    def __post_init__(self) -> None:
        if isinstance(self.seed, bool) or not isinstance(self.seed, int):
            raise ValueError(f"seed must be an integer; received {self.seed!r}")
        _require_positive_int("observation_size", self.observation_size)
        _require_positive_int("action_size", self.action_size)
        _validate_roles(self.roles)
        if not self.profiles:
            raise ValueError("profiles must contain at least one profile")
        invalid_profiles = [
            name for name in self.profiles if not name or not name.isidentifier()
        ]
        if invalid_profiles:
            raise ValueError(
                "profile names must be Python identifiers; "
                f"received {invalid_profiles}"
            )

    def dimensions(self, profile_name: str) -> PolicyDimensions:
        """Resolve one named profile into a complete model shape."""

        try:
            widths = self.profiles[profile_name]
        except KeyError as error:
            available = ", ".join(sorted(self.profiles))
            raise ValueError(
                f"unknown profile {profile_name!r}; available profiles: {available}"
            ) from error
        return PolicyDimensions(
            observation=self.observation_size,
            backbone=widths.backbone,
            head=widths.head,
            action=self.action_size,
            roles=self.roles,
        )


@dataclass(frozen=True)
class PolicyDimensions:
    """Complete tensor dimensions for one benchmark model profile."""

    observation: int
    backbone: int
    head: int
    action: int
    roles: tuple[str, ...]

    def __post_init__(self) -> None:
        _require_positive_int("observation dimension", self.observation)
        _require_positive_int("backbone dimension", self.backbone)
        _require_positive_int("head dimension", self.head)
        _require_positive_int("action dimension", self.action)
        _validate_roles(self.roles)


def _require_mapping(name: str, value: object) -> dict[str, Any]:
    """Narrow decoded JSON objects before reading named fields."""

    if not isinstance(value, dict):
        raise ValueError(f"{name} must be a JSON object")
    return value


def load_benchmark_config(
    path: str | Path = DEFAULT_CONFIG_PATH,
) -> BenchmarkConfig:
    """Load and validate the model portion of the benchmark configuration."""

    config_path = Path(path)
    try:
        raw = _require_mapping(
            "benchmark configuration",
            json.loads(config_path.read_text(encoding="utf-8")),
        )
        raw_profiles = _require_mapping("profiles", raw["profiles"])
        profiles: dict[str, ProfileWidths] = {}
        for name, values in raw_profiles.items():
            raw_profile = _require_mapping(f"profiles.{name}", values)
            profiles[name] = ProfileWidths(
                backbone=_require_positive_int(
                    f"profiles.{name}.backbone_width",
                    raw_profile["backbone_width"],
                ),
                head=_require_positive_int(
                    f"profiles.{name}.head_width",
                    raw_profile["head_width"],
                ),
            )
        raw_roles = raw["roles"]
        if not isinstance(raw_roles, list) or not all(
            isinstance(role, str) for role in raw_roles
        ):
            raise ValueError("roles must be a JSON array of strings")
        return BenchmarkConfig(
            seed=raw["seed"],
            observation_size=raw["observation_size"],
            action_size=raw["action_size"],
            roles=tuple(raw_roles),
            profiles=profiles,
        )
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(
            f"invalid benchmark configuration at {config_path}: {error}"
        ) from error


class SharedBackbone(nn.Module):
    """Two-layer feature extractor reused by every policy role."""

    def __init__(self, dimensions: PolicyDimensions) -> None:
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(dimensions.observation, dimensions.backbone),
            nn.ReLU(),
            nn.Linear(dimensions.backbone, dimensions.backbone),
            nn.ReLU(),
        )

    def forward(self, observations: Tensor) -> Tensor:
        """Convert observation rows into shared feature rows."""

        return self.layers(observations)


class PolicyHead(nn.Module):
    """Role-specific action layers applied to shared features."""

    def __init__(self, dimensions: PolicyDimensions) -> None:
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(dimensions.backbone, dimensions.head),
            nn.ReLU(),
            nn.Linear(dimensions.head, dimensions.action),
        )

    def forward(self, features: Tensor) -> Tensor:
        """Convert shared feature rows into action rows for one role."""

        return self.layers(features)


class SharedPolicyBenchmarkModel(nn.Module):
    """One seeded backbone with an ordered head for every configured role."""

    def __init__(self, dimensions: PolicyDimensions, seed: int) -> None:
        super().__init__()
        self.dimensions = dimensions
        self.seed = seed

        # Module constructors consume PyTorch's RNG. Forking keeps deterministic
        # benchmark initialization from changing randomness elsewhere in tests.
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed)
            self.backbone = SharedBackbone(dimensions)
            self.heads = nn.ModuleDict(
                {role: PolicyHead(dimensions) for role in dimensions.roles}
            )

        self.requires_grad_(False)

    @property
    def roles(self) -> tuple[str, ...]:
        """Return the stable output order used by export and routing code."""

        return self.dimensions.roles

    def forward(self, observations: Tensor) -> tuple[Tensor, ...]:
        """Return one `[batch, action]` tensor per role in configured order."""

        if observations.ndim != 2:
            raise ValueError(
                "observations must have rank 2 with shape [batch, observation]"
            )
        if observations.shape[1] != self.dimensions.observation:
            raise ValueError(
                "observations have the wrong feature width; expected "
                f"{self.dimensions.observation}, received {observations.shape[1]}"
            )
        if observations.dtype != torch.float32:
            raise TypeError(
                "observations must use torch.float32; "
                f"received {observations.dtype}"
            )

        features = self.backbone(observations)
        return tuple(self.heads[role](features) for role in self.roles)


class MaskedPolicyModel(nn.Module):
    """Architecture B: compute every head, then select with runtime masks."""

    def __init__(self, source: SharedPolicyBenchmarkModel) -> None:
        super().__init__()
        self.policy = deepcopy(source)
        self.role_count = len(source.roles)

    def forward(self, observations: Tensor, role_masks: Tensor) -> Tensor:
        """Combine all head outputs using one mask row per observation."""

        if role_masks.ndim != 2 or role_masks.shape[1] != self.role_count:
            raise ValueError(
                "role_masks must have shape [batch, role_count] with "
                f"role_count={self.role_count}"
            )
        if role_masks.dtype != torch.float32:
            raise TypeError(
                "role_masks must use torch.float32; "
                f"received {role_masks.dtype}"
            )

        all_actions = torch.stack(self.policy(observations), dim=1)
        return torch.sum(all_actions * role_masks.unsqueeze(-1), dim=1)


class CompleteRoleModel(nn.Module):
    """Architecture C: one complete backbone-plus-head model for one role."""

    def __init__(
        self,
        source: SharedPolicyBenchmarkModel,
        role: str,
    ) -> None:
        super().__init__()
        if role not in source.roles:
            raise ValueError(f"unknown role {role!r}")
        self.backbone = deepcopy(source.backbone)
        self.head = deepcopy(source.heads[role])

    def forward(self, observations: Tensor) -> Tensor:
        """Run one role's complete policy from observations to actions."""

        return self.head(self.backbone(observations))


class BackboneModel(nn.Module):
    """Architecture E stage one: expose shared features as model output."""

    def __init__(self, source: SharedPolicyBenchmarkModel) -> None:
        super().__init__()
        self.backbone = deepcopy(source.backbone)

    def forward(self, observations: Tensor) -> Tensor:
        """Convert observations into features for a later head model."""

        return self.backbone(observations)


class RoleHeadModel(nn.Module):
    """Architecture E stage two: one role head that accepts shared features."""

    def __init__(
        self,
        source: SharedPolicyBenchmarkModel,
        role: str,
    ) -> None:
        super().__init__()
        if role not in source.roles:
            raise ValueError(f"unknown role {role!r}")
        self.head = deepcopy(source.heads[role])

    def forward(self, features: Tensor) -> Tensor:
        """Convert shared features into actions for one role."""

        return self.head(features)


def build_seeded_policy(
    config: BenchmarkConfig,
    profile_name: str,
) -> SharedPolicyBenchmarkModel:
    """Build one frozen model whose parameters can seed every architecture."""

    return SharedPolicyBenchmarkModel(
        dimensions=config.dimensions(profile_name),
        seed=config.seed,
    ).eval()
