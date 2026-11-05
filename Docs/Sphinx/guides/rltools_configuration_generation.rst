Project RLtools configurations
==============================

Developers can choose their NPC's observation/action dimensions and supported
network/algorithm settings without writing RLtools C++ templates. A configuration
asset exports a project request, and UnrealBuildTool generates and compiles the
matching learner on the next project build. SAC, TD3 and PPO use the same workflow.

The two shipped shapes remain available. They are defaults, not a restriction on
project-defined shapes. No compilation occurs inside a running or packaged game.
New configurations require the Schola module's source and Unreal's C++ toolchain;
compiling Blueprints alone cannot extend a precompiled plugin binary.

Editor workflow
---------------

1. In the Content Browser, create a Data Asset of class
   ``RLToolsConfigurationAsset``.
2. Set ``InteractionDefinition`` using Schola's Box observation and bounded
   continuous Box action spaces. Set a project-local configuration name and
   choose SAC, TD3 or PPO.
3. Configure actor/critic networks, batch/storage sizes and applicable compiled
   algorithm parameters. Save the asset.
4. Click **Generate Project Configuration** in its Details panel (also a
   Blueprint-callable editor operation). Check ``GenerationStatus``.
5. Stop PIE, close the editor and build the Unreal project normally. No clean
   build, plugin-source edits, Python generator or manual template registration
   is required.
6. Reopen the project and assign the asset to
   ``URLToolsNPCPolicy.ConfigurationAsset``. Initialize with the NPC's Schola
   definition and wait for Ready as described in :doc:`native_adaptive_policy`.

For example, a 6-observation / 3-action SAC configuration can use a width-24,
four-layer actor and width-16, three-layer critic. It is discovered as
``sac/project/<name>`` after rebuilding.

.. code-block:: cpp

   // Config is a retained URLToolsConfigurationAsset selected by the developer.
   Policy = NewObject<URLToolsNPCPolicy>(this);
   Policy->ConfigurationAsset = Config;
   Policy->Settings.LearningStarts = Config->BuildSettings.BatchSize;
   Policy->Init(Config->InteractionDefinition);
   // On later ticks, wait for Ready; request actions and submit outcomes normally.

When an asset is assigned, it selects the algorithm/configuration. The legacy
Algorithm/Configuration fields are disabled in Details and remain available for
existing callers that select an already-compiled identifier directly. Runtime
settings, sharing and lifecycle behavior are unchanged.

Supported build choices
-----------------------

* Any positive flattened observation/action sizes within checked 32-bit index
  limits; Schola validates the original Box bounds and multidimensional shape.
* CPU feedforward MLP actor and critic with independent uniform hidden widths,
  3..16 total dense layers (including output), and ReLU or Tanh hidden activation.
  Output layers/distributions retain their algorithm-defined behavior.
* A compiled batch size of at least two. SAC/TD3 replay capacity must cover a
  batch. PPO rollout length must cover a batch and be divisible by it.
* Discount; SAC/TD3 target update rate and actor/target intervals; SAC initial
  entropy coefficient and adaptive entropy; TD3 target smoothing; PPO clipping,
  advantage estimation, entropy coefficient and action-distribution choices.

The pinned MLP implementation declares a zero-sized hidden-layer array at two
total layers; that layout is rejected before compilation. Other unsupported
architectures, activations and unknown manifest fields fail explicitly.

Network/storage dimensions and algorithm-template parameters require generation
and recompilation when changed. Bounds and shapes with the same flattened sizes,
rewards, episode conditions and runtime settings do not require new template
instantiations. Runtime settings include seed, warm-up, exploration, learning rate,
queues, scheduling, PPO epochs and fixed preprocessing.

Configuration generation does not collect observations or rewards automatically.
It compiles the numerical configuration used by the existing Schola policy API.

Missing and stale configurations
--------------------------------

``GetCompiledConfiguration`` compares the asset's requested settings with the
actual compiled profile, preserving its output on failure. Missing or changed
dimensions, networks, batch/storage settings or applicable algorithm parameters
produce Generate/Rebuild guidance. The policy carries this diagnostic through
``GetLastError``; an invalid replacement preserves an existing valid binding.

After editing an asset, save it, generate its request again and rebuild. Editing
an asset alone does not change the binary. Runtime controls still belong on the
policy. Captured snapshots and live learner settings do not change when an asset
is edited.

A configuration name identifies one algorithm/profile within a project. Repeated
generation updates the same asset-owned request. Changing its configuration name
or algorithm replaces its previous request. A different asset cannot overwrite a
name already owned by another request. Remove a request before moving/renaming
its owning asset, or update the request's source identity in version control.

**Remove Project Configuration** removes the asset-owned request; rebuild to
remove that profile from the executable. Removing a configuration still referenced
by a policy makes initialization fail with a missing-configuration diagnostic.

Build request and generated files
---------------------------------

The version-controlled build input is
``<Project>/Config/ScholaRLTools.json``. Save both this file and its owning
configuration assets. UBT reads the text request because configuration types must
be known before the editor loads assets.

A headless/C++ workflow may author the same versioned JSON directly:

.. code-block:: json

   {
     "schemaVersion": 1,
     "profiles": [{
       "name": "guards",
       "algorithm": "SAC",
       "observations": 6,
       "actions": 3,
       "network": {
         "actorHiddenWidth": 24,
         "criticHiddenWidth": 16,
         "actorLayers": 4,
         "criticLayers": 3,
         "actorActivation": "Tanh",
         "criticActivation": "ReLU"
       },
       "batchSize": 8,
       "replayCapacity": 256,
       "parameters": { "discount": 0.97, "targetTau": 0.02 }
     }]
   }

Optional fields use the reflected build-setting defaults. ``source`` records
request ownership; the build does not load that asset path. For PPO, use
``rolloutLength`` instead of ``replayCapacity``. Algorithm-specific parameter
names match the asset's fields in camelCase (for example ``ppoClip``,
``adaptiveEntropy`` and ``targetActionNoise``). Inapplicable fields are rejected,
rather than silently ignored.

The generator validates the entire manifest before emitting code: schema version,
field types, duplicate keys/identifiers, supported choices, finite parameter
ranges, batch/storage relationships and index-product overflow. A project request
is limited to 64 profiles and 4 MiB. Compilation and memory costs grow with the
selected configurations; these checks are not a memory/performance guarantee.

Generated parameter and registration headers live under
``<Project>/Intermediate/ScholaRLTools/<content-hash>/``. Identical effective
requests reuse unchanged output; changed or removed requests select a different
generated registry. UBT tracks the manifest as an external dependency, including
its creation/removal. Malformed input fails the build rather than falling back to
stale generated code.

The generator never modifies the plugin's shipped registry or vendored RLtools
headers. Project directories have separate generated output and profile sets.
The standalone native test target still builds the shipped profiles without a
project manifest. Packaged binaries use their compiled registry and do not read
JSON or compile new types at runtime.

Example and verification
------------------------

``Examples/NativeLearning/Config/ScholaRLTools.json`` contains three requests
exported through the configuration asset API: SAC 6/3, TD3 7/4 and PPO 8/2, with
different actor/critic architectures and non-default algorithm parameters.

.. code-block:: powershell

   ./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
   ./Test/rltools_generation/Run.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'

Unreal automation tests exercise asset validation, request ownership/update/removal,
missing/stale diagnostics and actual policy learning through compiled generated
configurations. The generator tests compile the same C# implementation used by
UBT and cover malformed requests, determinism, invalidation and project isolation.
The editor export test writes its fixture only under Saved; it never changes the
running project's build request.
