# Advanced NPCs for AMD Schola - Individual Contributions (D3)

> Complete the remaining team entries before submission.

**[Name]**, [Role]
> [Specific contributions: features built, PRs reviewed, infrastructure set up, etc.]

**[Name]**, [Role]
> [Specific contributions]

**[Name]**, [Role]
> [Specific contributions]

### Clementine Yang — Shared Policy Unreal Integration

- **PR #32 – macOS ARM64 Support:** Added macOS build scripts and dependency configuration for Schola. Successfully built gRPC dependencies and compiled the BackgroundNPCsEditor project on Apple Silicon using Unreal Engine 5.8.

- **PR #33 – Unreal NNE Testing:** Implemented Unreal Automation Tests for Shared Policy architectures A, C, and D using `NNERuntimeORTCpu`.

- **Reference Testing:** Compared Unreal inference outputs against Python ONNX Runtime results, checking numerical accuracy and tensor shapes across different batch sizes.

- **Documentation:** Added macOS setup instructions and documented the build and testing process.

- **Collaboration:** 
  
**[Name]**, [Role]
> [Specific contributions]

### Max Xu — Background NPCs, Scaling the Backend

Built CampusCrowd to demonstrate Schola’s background inference in a campus scene with up to 100,000 Mass agents. Imported campus geometry, added pedestrian routes, camera controls, performance reporting, and scripts for building, packaging, and comparing inference modes. Added CAD import tests and geometry checks. Scaled the inference queue with crowd size and made missed decision targets visible. Documented measured performance, local testing and packaging commands. Built and packaged CampusCrowd, checked demo controls, and retained logs, reports, and screenshots for a standalone 20,000-agent run at 59.98 FPS and 199,922 decisions/s. [D3 validation evidence](../../Examples/CampusCrowd/Evidence/d3-validation-2026-11-05/README.md). Produced a demo video showing **20,000 agents making 200,000 decisions per second at 60 FPS**. [Watch the CampusCrowd demo](https://utoronto-my.sharepoint.com/:v:/g/personal/maxh_xu_mail_utoronto_ca/IQDEpcI8CITpT6vs0vv20yCwAU4lH9gFIKJzbEZhq47R4p8?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=R3tV6g).

[Implementation](../../Examples/CampusCrowd/README.md) · [Recorded results](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/blob/92c4fd7708c136218e5cd79cc6d240b217307ccb/Examples/CampusCrowd/README.md#performance). [Merged PR #25](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/25).
