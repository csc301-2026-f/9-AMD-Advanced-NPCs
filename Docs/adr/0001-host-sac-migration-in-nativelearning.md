---
status: accepted
---

# Host the SAC-to-plugin migration in NativeLearning, not AdaptiveTag or a new project

Migrating the D3 chase adaptation experiment onto the real Schola plugin backend
(`URLToolsNPCPolicy`) needed a host project. `AdaptiveTag` was rejected because
its whole module is built around the example-owned prototype learner
(`OnlineSACLearner`, `ChaseSession`) — untangling that coupling would be most of
the migration work before any new code ran. A fourth example project was
rejected because it would duplicate `.uproject`/build-script/CI scaffolding that
already exists. We extended `Examples/NativeLearning` instead: it already depends
only on the plugin, carries no Chase-specific code, and already uses the
generated-profile workflow (`Config/ScholaRLTools.json` + the generator
commandlet) that a new chase-shaped profile needs anyway.

The trade-off this accepts: `NativeLearning` was headless (`-nullrhi`,
no rendering) before this change, so making it playable required adding a
minimal visual scene and HUD to a project that was previously test-only. That
is a deliberate, scoped addition, not scope creep — `AdaptiveTag` remains the
richer, playable prototype for reference and fast iteration (see
`deliverables/D3` evidence).
