# Advanced NPCs for AMD Schola

> Deliverable 3 product evolution scaffold. Replace the placeholders with the
> team's actual work and evidence. The course template suggests 1000–2000 words,
> with concise analysis of reasoning, impact, and integration quality.

## 1. What Needed to Improve

**Most important gap or limitation after D2:** [Describe the gap]

**How we identified it:** [User feedback, testing, partner input, or team reflection]

**Why addressing it matters:** [Affected user or workflow and expected benefit]

**Shared Policy Team**
In D2, we had a working Shared Policy prototype with a shared backbone and multiple specialized heads, but it was mainly tested in Python. We needed to verify its compatibility with Unreal Engine and explore different architectures to understand their potential performance trade-offs.

## 2. What You Did

**Implemented improvement:** [Describe the completed work]

**Architectural or design changes:** [Key decisions and trade-offs]

**Integration challenges:** [What was difficult and how the team addressed it]

**Supporting artifacts:** [Relevant PRs, release, or implementation links]

**Shared policy Team**
We developed a benchmark prototype to compare five architectures (A–E) and evaluate their inference behaviour (PR #24). We also added macOS ARM64 build support for Schola (PR #32) and implemented Unreal NNE Automation Tests for architectures A, C, and D. All three passed and their outputs were validated against Python ONNX Runtime reference results (PR #33).

## 3. Before vs. After

| Workflow or quality | Before D3 | After D3 | Supporting evidence |
| --- | --- | --- | --- |
| [Aspect improved] | [D2 baseline] | [Observed D3 improvement] | [Demo, test, feedback, or measurement] |

[Explain the impact and any limits of the comparison. Use comparable conditions
for measurements.]

## 4. Reflection

**What worked well:** [Approach and its effect]

**What was harder than expected:** [Challenge and lesson learned]

**Path to D4:** [Remaining work, risks, and next steps]

**Shared Policy Team**
D3 helped us move beyond the initial prototype and verify that multiple Shared Policy architectures can run in Unreal. We also learned that supporting selective inference does not automatically guarantee better performance. Next, we plan to complete NPC head-routing integration, improve the benchmark, and evaluate the architectures under more realistic conditions.
