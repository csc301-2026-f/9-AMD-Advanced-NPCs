Native RLtools learning
=======================

Schola's existing Python training integration lets a developer define a game
environment, select an algorithm and configure its networks and learning
settings. Schola constructs a Stable Baselines3 or RLlib algorithm and starts
training; the developer supplies the task rather than implementing the learner.

The native integration adds the corresponding learning implementation in C++,
inside the Unreal process. RLtools supplies the neural-network and algorithm
operations. ``ScholaRLTools`` constructs and maintains their state, accepts
completed gameplay experiences, schedules updates and supplies updated policies
for inference.

Implemented numerical learning
------------------------------

The private ``Learning/RLToolsLearner.h`` API is for Schola's native policy and
world service. It currently implements CPU feedforward SAC, TD3 and PPO.
The public Unreal policy/settings adapter and world service are described in
:doc:`native_adaptive_policy`. Automatic gameplay coordination is separate work.

.. list-table:: Existing Schola and the native implementation
   :header-rows: 1
   :widths: 35 65

   * - Existing integration
     - Native implementation
   * - ``FSB3TrainingSettings`` selects an algorithm and network/algorithm settings.
     - Compiled configuration descriptors and validated runtime settings select
       a matching RLtools implementation.
   * - The Python training script constructs the selected SB3 model.
     - ``FLearner::Create`` allocates and initializes the selected RLtools state.
   * - SB3 manages replay or rollout storage and trains through ``model.learn``.
     - ``Submit`` admits owned experiences; ``Train`` stores them and calls the
       selected RLtools training operations.
   * - ``UNNEPolicy`` evaluates an imported ONNX model.
     - A native policy can evaluate the learner's published actor directly.
       Each captured actor remains unchanged while later training continues.

``FInteractionDefinition``, ``FPoint`` and existing environment outcomes remain
the public representations. The native policy adapter validates/converts those
values at the backend boundary. A numerical decision retains its originating
policy, observation, agent/episode identity and algorithm-specific information.
The game does not compute PPO probabilities or manipulate RLtools buffers.

Algorithms and experience
-------------------------

SAC and TD3 store experiences in RLtools replay buffers. Warm-up, collection
frequency and the number of training cycles are separate controls. SAC updates
its critics, actor, targets and (when configured) entropy temperature. TD3
applies its delayed actor and target updates and target-action smoothing.

PPO stores a bounded recent rollout. Each decision retains its original Gaussian
action and log probability. The applied command is clipped to the normalized
action range; training uses the original sampled action for its likelihood.
An additional gameplay action change is rejected for PPO. Advantages stop at
agent/episode changes, gaps and episode boundaries. Truncations bootstrap from
their final observation; terminations do not.

PPO admits only the current policy version. During an update it temporarily
closes admission with ``NotReady`` while inference remains available. Queued
outcomes from an older published policy are retired and counted as ``Stale``.
One worker call can execute a bounded number of epochs. The updated actor is
published when the configured epochs for that rollout finish.

Configuration and supported choices
-----------------------------------

``FLearner::GetProfiles`` reports the actual compiled algorithms and dimensions.
The initial configurations are:

.. list-table::
   :header-rows: 1

   * - Identifier suffix
     - Observations/actions
     - Network
     - Batch/replay/PPO rollout
   * - ``3x2-mlp32``
     - 3 / 2
     - Two hidden layers of width 32, ReLU
     - 32 / 4096 / 64
   * - ``5x1-mlp32``
     - 5 / 1
     - Two hidden layers of width 32, ReLU
     - 32 / 4096 / 64

Prefix an identifier with ``sac/``, ``td3/`` or ``ppo/``. These are internal
configuration identifiers. :doc:`rltools_configuration_generation` derives
project-local entries from Schola definitions/settings at build time. An
unavailable configuration fails with build guidance. A packaged game never
compiles new types.

Structural parameters are defined in ``RLToolsLearnerParameters.h``. The
generated parameter header and registration include provide the extension
point for project generation: discount, target update rates,
SAC entropy choices, TD3 target noise, PPO clipping, advantage estimation and
action-distribution options. The second shipped configuration uses discount
0.95 and PPO clipping 0.15 to exercise non-default instantiations. The first
uses discount 0.99 and PPO clipping 0.2.

Runtime ``FSettings`` controls seed, queue capacity, learning starts, initial
random exploration, collection/training frequency, update bursts, publication,
PPO epochs, learning rate and TD3 exploration noise. Optional fixed observation
means/standard deviations are validated and copied into snapshots; observations
are otherwise unchanged. Running normalization, recurrent models, additional
devices and checkpoint persistence are not implemented. Their presence
in RLtools is not reported as available Schola functionality.

Execution and lifecycle
-----------------------

``Train`` belongs on a worker selected by the world learning service. A learner
does not create a thread per NPC. Training one learner is serialized; inference
reads immutable policy state without waiting for training. Different agents
have separate inference random sequences. Training has its own RNG. A full
queue drops the new record without consuming its decision. Duplicate submissions
and decisions from another learner or generation fail.

Pause immediately closes admission with ``Frozen``. The active numerical update
may finish, after which ``ReadStats().Training`` becomes false. Already admitted
records remain available on resume. Stop closes admission and inference.
Final destruction waits for an active training burst. ``Reset`` is a worker-only
replacement that clears training state and retires old decisions; captured
snapshots remain usable. Ordinary encounter reset is handled by the common
policy lifecycle and does not invoke learning reset.

``FStats`` reports admission counts, stored experience, queue/replay/rollout
occupancy, stale PPO records, pending work, published version and burst time.
``Updates`` counts SAC/TD3 training cycles or PPO epochs. ``ActorUpdates`` and
``CriticUpdates`` count optimizer steps; one PPO epoch can contain several
minibatches. Burst limits are cooperative boundaries; an individual RLtools
update is not preemptible.

Verification
------------

``Prototypes/NativeLearning`` compiles the exact plugin learner without Unreal.
It verifies all three algorithms and both shapes, real actor changes, immutable
snapshots, PPO/TD3 scheduling, admission errors, lifecycle and concurrent use.
The ``Schola.RLTools`` Unreal automation tests verify module linkage, algorithm
construction, experience/training/publication and controls in the engine host.
``Schola.Inference.Adaptive.BackendSpaces`` verifies that a controlled backend
can use categorical Schola spaces through the common lifecycle; it does not
claim that the current RLtools implementations support categorical actions.

Successful training calls establish implementation correctness, not useful NPC
adaptation. Task-specific improvement and packaged gameplay are separate checks.
