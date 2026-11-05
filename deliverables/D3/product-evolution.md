# Advanced NPCs for AMD Schola

> Team draft. Complete the remaining team placeholders before submission.

## 1. What Needed to Improve

**Most important gap or limitation after D2:** [Describe the gap]

**How we identified it:** [User feedback, testing, partner input, or team reflection]

**Why addressing it matters:** [Affected user or workflow and expected benefit]

**Shared Policy Team**： In D2, we had a working Shared Policy prototype with a shared backbone and multiple specialized heads, but it was mainly tested in Python. We needed to verify its compatibility with Unreal Engine and explore different architectures to understand their potential performance trade-offs.

### Background NPCs — Max Xu

D2 demonstrated background inference with 20,000 Mass agents in a comparison scene. It did not show how the same API would fit a large campus with buildings and pedestrian routes. This gap follows from reviewing the D2 demo’s scope; no new partner feedback is claimed. CampusCrowd makes that integration visible and tests how rendering, movement, and inference interact at larger crowd sizes.

## 2. What You Did

**Implemented improvement:** [Describe the completed work]

**Architectural or design changes:** [Key decisions and trade-offs]

**Integration challenges:** [What was difficult and how the team addressed it]

**Supporting artifacts:** [Relevant PRs, release, or implementation links]

**Shared policy Team**： We developed a benchmark prototype to compare five architectures (A–E) and evaluate their inference behaviour (PR #24). We also added macOS ARM64 build support for Schola (PR #32) and implemented Unreal NNE Automation Tests for architectures A, C, and D. All three passed and their outputs were validated against Python ONNX Runtime reference results (PR #33).

### Background NPCs — CampusCrowd

Built a 1.3 × 1.4 km campus demo using imported Toronto building geometry and up to 100,000 Mass students. It reuses Schola’s background inference API and the D2 ONNX model. Mass entities provide crowd simulation; background workers batch model calls while the game thread gathers observations and applies completed actions. Route following and local separation combine with model steering.

The main scaling issue was queue capacity. At 100,000 students, the default per-frame submission limit leaves a backlog. Higher queue and submission limits restore the requested decision rate, but do not restore 60 FPS. The HUD reports achieved throughput and highlights rates below 95% of target; timed captures also return exit status 3 when that threshold is missed. This prevents a smooth frame rate from hiding missed decisions.

Build, package, comparison, and capture scripts support repeatable local runs. Recorded CAD checks report five passing import tests, valid geometry, and routes clear of buildings. Implementation was merged in [PR #25](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/25). Review evidence should be checked on the PR before submission.

Evidence: [implementation and setup](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md), [CAD import tests](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/cad-import-tests.json), [geometry validation](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/geometry-validation.json), and [same-scene comparison](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/Evidence/comparison.md).

Local automated tests and scripted builds/packages replace GitHub Actions for this Unreal workload: provisioning an engine image and native dependencies is unsuitable for the course’s short hosted jobs. This is the rubric’s documented local-process alternative, not deployment on merge. Five CAD regression tests and all 43 Unreal background/Mass tests passed on 5 November 2026. The editor build, packaging, and standalone smoke test also passed: 20,000 agents delivered 199,922 decisions/s at 59.98 FPS, with no dropped requests during capture. [Retained D3 validation](../../Examples/CampusCrowd/Evidence/d3-validation-2026-11-05/README.md). [Validation commands and NFR evidence](../../Examples/CampusCrowd/README.md#local-validation-and-deployment-d3) cover setup, tests, input bounds, overload errors, keyboard controls, and local packaging. The unused Android file server was disabled and its generated token removed from the current config.

## 3. Before vs. After

|Subteam| Workflow or quality | Before D3 | After D3 | Supporting evidence |
| --- | --- | --- | --- | --- |
|[Subteam Name]| [Aspect improved] | [D2 baseline] | [Observed D3 improvement] | [Demo, test, feedback, or measurement] |
|Background NPCs| Integration and scale | 20,000-agent comparison scene | Campus geometry, routes, camera controls, and runs up to 100,000 agents | [Recorded performance](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance) |
|Shared Policy| Architecture comparison | No systematic comparison | Evaluated five architectures (A–E) and their inference trade-offs | PR #24 |
|Shared Policy| Unreal compatibility | Mainly Python testing | A, C and D passed Unreal NNE tests | PR #33 |
|Shared Policy| macOS support | Limited build support | Added ARM64 build support | PR #32 |

[Explain the impact and any limits of the comparison. Use comparable conditions
for measurements.]

### CampusCrowd measurements

**20,000 agents making 200,000 decisions per second at 60 FPS.** [Watch the CampusCrowd demo](https://utoronto-my.sharepoint.com/:v:/g/personal/maxh_xu_mail_utoronto_ca/IQDEpcI8CITpT6vs0vv20yCwAU4lH9gFIKJzbEZhq47R4p8?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=R3tV6g).

On a Ryzen 5 7600X and RX 7800 XT at 1600×900 with a 60 FPS cap, two CPU inference workers, batches of 64, and 10 decisions/s per student:

| Students | FPS | Completed decisions/s | Target decisions/s |
| ---: | ---: | ---: | ---: |
| 20,000 | 60 | 200,000 | 200,000 |
| 50,000 | 60 | 499,000 | 500,000 |
| 100,000, default limits | 36 | 895,000 | 1,000,000 |
| 100,000, raised limits | 39 | 997,000 | 1,000,000 |

These rounded results come from the [recorded README](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance). They describe local runs, not a controlled D2-to-D3 speedup. A separate same-scene 2,000-agent comparison reduced game-thread inference from 1.83 to 0.34 ms/frame; both modes reached the 60 FPS cap. Background inference added about one frame of action latency. The scene demonstrates a production use case; cross-platform validation and a trained policy remain open.

## 4. Reflection

**What worked well:** [Approach and its effect]

**What was harder than expected:** [Challenge and lesson learned]

**Path to D4:** [Remaining work, risks, and next steps]

**Shared Policy Team**： D3 helped us move beyond the initial prototype and verify that multiple Shared Policy architectures can run in Unreal. We also learned that supporting selective inference does not automatically guarantee better performance. Next, we plan to complete NPC head-routing integration, improve the benchmark, and evaluate the architectures under more realistic conditions.

### Background NPCs — Max Xu

Reusing the existing scheduler made it possible to focus on scene integration and scale. Queue sizing was harder than increasing the agent count: at 100,000 students, admission limits caused a backlog while inference workers still had spare capacity. Reporting FPS and completed decisions together exposed that problem. Max’s D4 priority is UX and reducing boilerplate: simplify setup and controls, and make the API easier to integrate through reusable configuration. Game-thread scheduling cost, validation on another machine, and a trained policy remain technical limitations to track.
