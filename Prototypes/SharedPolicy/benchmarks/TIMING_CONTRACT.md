# Timing fairness contract

This audit defines what the shared-policy benchmark compares. It prevents a
shorter neural-network call from being reported as an end-to-end improvement if
grouping, copies, or output restoration erase that saving.

## Same starting request

For one workload, A–E receive the same prepared float32 observation array and
the same ordered role labels. Workload generation happens once before the
candidate loop. The runtime must not mutate either input, and every candidate
must return float32 actions in the original row order before timing is accepted.

Session creation, model loading, workload generation, first-run checks, warm-up,
profiling, and JSON serialization are outside warmed timing samples. Session
initialization and first runs are recorded separately.

## The two clocks

| Clock | Starts and stops | Included work | Excluded work |
| --- | --- | --- | --- |
| Complete request | Immediately around `runtime.run(...)` | Validation, role-ID creation, group-index creation, NumPy allocation/indexing/copies, every ONNX call, selection or masking, intermediate feature handoff, scattering, and final float32 conversion | Session initialization, correctness first runs, warm-up, profiling, and report serialization |
| Model calls | Immediately around each `session.run(...)` | ONNX Runtime execution and provider work for that invocation | Python grouping, NumPy slicing/copies, allocation, selection, scattering, call-record construction, and report work |

The sum of model-call times is therefore a component of complete-request time.
Their difference is reported as routing/host overhead; it is not interpreted as
pure grouping cost because it also contains timer and Python dispatch overhead.

## Candidate work inside complete-request time

All candidates first validate the request, convert role labels to integer IDs,
and build non-empty role groups. That shared work is currently included even for
A and B, which do not subsequently use the groups.

| Architecture | Model invocations for `K` active experts | Additional included host work |
| --- | ---: | --- |
| A — all heads | 1 | Stack all head outputs, then select the required head output for each original row |
| B — masked | 1 | Build a one-hot role mask; masking and reduction occur inside the ONNX invocation |
| C — complete model per expert | K | Allocate the restored output, copy each observation group through NumPy advanced indexing, and scatter each result back to its original rows |
| D — conditional ONNX | K | Perform the same grouping/copy/scatter work as C and create one scalar `If` condition for each homogeneous invocation |
| E — split backbone and heads | 1 + K | Run the backbone on the mixed batch, allocate the restored output, copy grouped intermediate features, invoke active heads, and scatter results back |

These paths intentionally do not contain equal amounts of host work. The point
is to compare equivalent deployable requests, not isolated graphs stripped of
their required routing. Model-call time diagnoses graph savings; complete-request
time determines whether those savings survive integration overhead.

## Fairness checks before full trials

- Use one deterministic workload per profile/batch/mixture comparison.
- Reuse persistent sessions with identical provider, thread, and optimization
  settings.
- Require equivalent restored actions before retaining performance results.
- Randomize candidate order reproducibly and retain the actual order.
- Run profiling separately from timed trials.
- Do not run overlapping benchmark workers.
- Report the measured platform; Mac CPU results do not establish Unreal, GPU,
  or AMD-hardware performance.
