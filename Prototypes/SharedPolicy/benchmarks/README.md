# Shared policy architecture benchmarks

Status: directory scaffold only; architecture implementations and benchmark
results are pending plan approval.

This experiment evaluates how a shared-backbone, multi-head policy should avoid
unused head computation. It directly tests the architectures raised in AMD
feedback: fixed masks, host-side expert routing, conditional ONNX execution, and
separate backbone/head models. The deliverable is complete only after the
portable ONNX Runtime comparison is followed by Unreal NNE compatibility and
performance validation.

Models use identical seeded weights and synthetic observations so architecture
cost is isolated from training quality. The experiment does not train a router,
learn role assignments, or claim useful NPC behavior.

## Candidate definitions

Let `B` be the number of NPC rows and `K` the number of nonempty roles in a
request. The host knows one role label for every row before inference.

| Candidate | Execution for one mixed-role request | Model calls |
| --- | --- | --- |
| A: all heads | One backbone on all `B` rows; every head on all feature rows; host selects each row's action | 1 |
| B: runtime masks | Same backbone and head computation as A; dynamic role masks combine head outputs inside the graph | 1 |
| C: complete model per role | Host groups observations; each nonempty group runs its role's backbone-plus-head model | `K` |
| D: conditional graph | Host groups observations; the same ONNX graph runs once per group with one Boolean role condition and executes one head after its backbone | `K` |
| E: separate backbone and heads | One backbone on all `B` rows; host groups features; each nonempty group runs its head model | `1 + K` |
| F: mixed-row routing inside one graph | One graph groups features by role, executes heads on their subsets, and restores row order | Research extension; graph/runtime feasibility unproven |

A–E are required. F is investigated only if the standard designs cannot satisfy
the target or if AMD specifically requires one mixed-role inference call.

C and D partition rows, so each NPC passes through a backbone once, although
separate role groups lose the efficiency of one larger backbone batch. C stores
one backbone copy per role; D stores one conditional model. E stores and executes
one backbone but adds intermediate tensor handoff and extra calls.

B is a negative control, not presumed optimization. Its mask must be a runtime
input so export-time constant folding cannot remove a head. Operator profiles
must establish which nodes actually execute. D is a required feasibility result:
successful selective execution and a precise unsupported-export/runtime result
are both reportable outcomes.

## Correctness contract

Every candidate returns float32 actions shaped `[B, 2]` in original NPC order.
For identical observations, role labels, and weights, selected actions must match
A within the documented numerical tolerance. Tests cover shuffled mixed roles,
homogeneous batches, singleton batches, invalid roles and shapes, empty-request
handling, exact call counts, and dynamic batch dimensions.

Correctness gates performance claims. A candidate that fails equivalence or
runtime validation is reported as unsupported, not benchmarked as a winner.

## Workload matrix

The initial controlled matrix uses two roles and sweeps:

- small, balanced, and expensive-head network profiles;
- batch sizes `1`, `16`, `64`, and `256`;
- homogeneous, approximately 50:50, and approximately 90:10 role mixtures;
- deterministic shuffled row order and identical observations per comparison.

After the two-role experiment is valid, add 4- and 8-role scaling workloads to
measure how model storage, call count, routing overhead, and unused-head cost grow
with the number of experts. Synthetic dimensions are stress profiles, not claims
about AMD's production network; real team dimensions should be added when known.

## Measurement contract

All sessions are created once and reused. Initialization and first-run latency are
reported separately from warmed inference. Timed trials run without profiling,
use identical provider/thread/optimization settings, randomize candidate order
reproducibly, and retain raw samples.

Two latency views are required:

1. **Model/component time:** each `session.run` and model stage, exposing whether
   skipped head work reduces neural execution cost.
2. **Complete-request time:** grouping, copies, every model call, intermediate
   handoff, and restoration of original row order, exposing whether the saving
   survives routing overhead.

Required evidence includes correctness, call counts, cold/first-run latency,
warmed p50/p95 latency, rows per second, model artifact bytes, operator profiles,
and isolated process/session memory. Runtime memory is measured in fresh
inference-only workers without export or PyTorch imports and is reported
separately from file size.

The portable screen begins with ONNX Runtime `CPUExecutionProvider`, sequential
execution, fixed intra/inter-op thread counts, and recorded OS/CPU/package/model
metadata. Results apply only to measured environments. Python results do not
establish Unreal, GPU, AMD-hardware, or gameplay performance.

## Unreal NNE validation

`Examples/BackgroundNPCs` on Unreal Engine 5.8 is the integration host because it
already loads ONNX through NNE and exposes inference metrics. Unreal validation
is required after the portable screen:

1. Build and run the unchanged host to establish a working NNE baseline.
2. Probe import/model-creation/execution compatibility for every viable A–E
   artifact, recording unsupported control flow or opsets precisely.
3. Integrate A and every supported candidate that skips work or presents a
   meaningful memory tradeoff under the same routing and output contract.
4. Measure isolated runs with persistent model instances, fixed workloads,
   warm-up, repeated trials, and raw evidence.

Required Unreal metrics are frame time/FPS, game-thread inference cost, model
execution time, decisions per second, action latency, calls, rows per call, and
process/session memory. The final recommendation must distinguish portable ORT
evidence from NNE evidence and name the platform/workload for every claim.

## Decision rule

No candidate wins merely because it returns the right output or zeros an unused
head. A recommendation requires:

- equivalent routed actions;
- profile evidence explaining what executed;
- repeated improvement larger than trial-to-trial variation in at least one
  declared target such as latency, throughput, or memory;
- routing/call/handoff costs included in the end-to-end result;
- Unreal NNE support and measurement for the proposed production path.

If no selective architecture beats A, the valid recommendation is to retain the
all-head model for the tested workload and document when larger heads or more
roles might change that decision.

## Planned source layout

| File | Responsibility |
| --- | --- |
| `models.py` | Configurable seeded backbone/head layers and architecture builders |
| `export_models.py` | Export, validate, and manifest A–E ONNX artifacts |
| `runtime.py` | Persistent sessions, role grouping, staged execution, and row-order restoration |
| `benchmark.py` | Correctness gate, warm-up, component/end-to-end timing, profiling, memory workers, and result serialization |
| `config.json` | Dimensions, workloads, seed, runtime settings, warm-up, repetitions, and tolerances |
| `evidence/` | Selected reproducible configurations, raw results, profiles, Unreal reports, and analysis |

Correctness tests live in the existing `../tests/` directory. The parent
prototype's Python environment provides dependencies. Generated ONNX models go
in ignored `artifacts/`; temporary runs and profiles go in ignored `runs/`.
Source, configuration, and curated evidence remain reviewable in Git.

## References

- [ONNX If operator](https://onnx.ai/onnx/operators/onnx__If.html): its condition
  contains one Boolean element, not one independent condition per row.
- [ONNX Runtime profiling](https://onnxruntime.ai/docs/performance/tune-performance/profiling-tools.html):
  operator evidence is collected separately from reported timings.
- [ONNX Runtime thread management](https://onnxruntime.ai/docs/performance/tune-performance/threading.html):
  thread counts and execution mode affect comparisons.
- [PyTorch ONNX export](https://docs.pytorch.org/docs/stable/onnx.html): exported
  control flow and dynamic dimensions must be validated with installed versions.
