# Shared Policy Prototypes

This directory contains two related pieces of work:

- `legacy/`: the original small ONNX reference prototype;
- `benchmarks/`: the new experiment comparing architectures A–E.

Both use the same Python environment defined at this directory's top level.

## Directory layout

```text
SharedPolicy/
├── legacy/       # Original model, exporter, demo, and tests
├── benchmarks/   # New A–E benchmark source, configuration, and tests
├── pyproject.toml
├── pytest.ini
└── uv.lock
```

## Original reference prototype

The `legacy/` prototype uses deterministic weights so its tensor names,
batching, outputs, and routing can be verified without training an agent.

## Contract

| Tensor | Type | Shape | Meaning |
|---|---|---|---|
| `observations` | `float32` | `[batch_size, 4]` | One observation row per NPC |
| `combat_actions` | `float32` | `[batch_size, 2]` | Combat-head output for every row |
| `support_actions` | `float32` | `[batch_size, 2]` | Support-head output for every row |

The ONNX graph contains one shared backbone and both specialized heads:

```text
observations [B, 4]
        |
        v
shared Linear(4 -> 3) + ReLU
        |
        +--------------------+
        |                    |
        v                    v
combat Linear(3 -> 2)   support Linear(3 -> 2)
        |                    |
        v                    v
combat_actions [B, 2]   support_actions [B, 2]
```

## Run from a fresh checkout

Python 3.12 and [`uv`](https://docs.astral.sh/uv/) are required.

```bash
cd Prototypes/SharedPolicy
uv sync --python 3.12
uv run pytest -q
uv run python -m legacy.run_shared_policy_demo
```

The final command regenerates the ignored `shared_policy.onnx` file, validates
it with ONNX, creates one ONNX Runtime session, and executes one model call for
the two-row batch.

Expected demo output:

```text
observations shape: (2, 4)
combat_actions shape: (2, 2)
support_actions shape: (2, 2)
NPC 0 -> combat: [4.5, 5.5]
NPC 1 -> support: [4.5, -1.0]
ONNX Runtime calls for the batch: 1
Limit: this does not prove Unreal NNE RunSync integration.
```

## What the demo proves

- One exported neural-network model contains the shared backbone and both heads.
- The input and both outputs have a symbolic `batch_size` dimension.
- One `onnxruntime.InferenceSession.run` call handles two NPC observation rows.
- Routing happens after inference: row 0 selects `combat_actions`, while row 1
  selects `support_actions`.
- Fixed weights produce reproducible outputs, and invalid requests fail before
  model execution.

## What the demo does not prove

- Unreal NNE model loading or `RunSync` behavior.
- C++, Blueprint, gameplay, or `USimpleStepper` integration.
- Reinforcement-learning training or useful learned NPC behavior.
- Production performance, heterogeneous action shapes, or the final ownership
  of head-selection metadata.

Production Unreal integration requires a supported Unreal installation, a host
`.uproject`, and a separate test showing one real NNE `RunSync` call for a batch.

## Source map

- `legacy/shared_policy.py`: deterministic PyTorch architecture and fixed weights.
- `legacy/export_shared_policy.py`: named dynamic-batch ONNX export and validation.
- `legacy/run_shared_policy_demo.py`: one-call ONNX Runtime execution and row routing.
- `legacy/tests/`: tests for the original prototype.
- `benchmarks/README.md`: experiment definitions and current implementation status.
- `benchmarks/tests/`: tests for the new benchmark implementation.
