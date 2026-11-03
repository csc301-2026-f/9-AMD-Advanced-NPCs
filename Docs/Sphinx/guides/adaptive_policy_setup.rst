Adaptive Policy Records and Local Setup
============================================================

This guide describes the implemented foundation for adaptive NPCs: shared
Unreal data records and validation of the NPC's input/output definitions. The
native gameplay-to-learning components and their current limits are described in
:doc:`adaptive_online_learning_contract`.

The policy's nonblocking ticket lifecycle is described in
:doc:`adaptive_decision_lifecycle`. The base has no learner; tests use controlled
native responses. ``UOnlineSACPolicy`` in the AdaptiveSAC example subclasses it
with an in-process RLtools learner. The mock Adaptive selection on
``UNPCInferencePolicy`` remains separate from this native integration.

What each type represents
--------------------------------------------------

.. list-table:: Foundation types
   :header-rows: 1
   :widths: 30 70

   * - Type
     - Meaning
   * - ``FAdaptiveDecision``
     - An action and the session, NPC, encounter, request, and policy-version
       identifiers needed to match it to its origin.
   * - ``FAdaptiveDecisionRequest``
     - A copied observation and the session, NPC, encounter and ticket identity
       assigned by the bridge when it dispatches a decision request.
   * - ``FAdaptiveExperience``
     - A completed outcome: those identifiers, the originating observation,
       command actually executed, reward, next observation, and separate
       termination/truncation flags.
   * - ``EAdaptiveSessionState``
     - Local lifecycle: Uninitialized, Initialized, Ready, Failed and Stopped.
   * - ``EAdaptiveRequestStatus``
     - Request results: Unknown, Pending, Ready, Failed, Cancelled, and Expired.
   * - ``UAdaptiveNPCPolicy``
     - An Unreal policy object that validates and copies the declared
       observation/action spaces. It implements ``IPolicy`` while immediate
       inference remains unavailable.

Include ``Policies/AdaptiveLearningTypes.h`` for the records and status types,
or ``Policies/AdaptiveNPCPolicy.h`` for the policy. Add ``ScholaInferenceUtils``
to the consuming module's dependencies. All types are available to Blueprint.

Records are passive containers. Assigning a reward or action does not validate,
submit or learn from an experience. A finite negative reward is
legitimate learning data. Do not put a preferred action or an unrelated fallback
into ``ExecutedAction``.

The initial Unreal representation uses ``FGuid`` for ``SessionId`` and signed
64-bit integers for ``AgentId``, ``EpisodeId``, ``DecisionId``, and
``PolicyVersion``. A default record has an invalid GUID and numeric IDs set to
``INDEX_NONE`` (-1), meaning unassigned; zero is a valid numeric identity/version.
Base initialization does not generate session or decision IDs. A native learner
subclass starts a local session with a fresh GUID and valid agent/episode IDs;
the base assigns decision IDs when requests are dispatched. These are local
Unreal records.

Supported local definitions
---------------------------

This first implementation accepts the following numerical subset. Dimensions
are supplied by the game rather than fixed to the pursuit example.

.. list-table:: Accepted spaces
   :header-rows: 1
   :widths: 23 77

   * - Definition
     - Requirements
   * - Observation space
     - A nonempty ``FBoxSpace``. Bounds may be finite, one-sided, or unbounded.
       Every enabled bound must be finite. An enabled lower bound cannot exceed
       an enabled upper bound. Constant observation dimensions are allowed.
   * - Action space
     - A nonempty ``FBoxSpace``. Each dimension needs finite lower and upper
       bounds, with a strictly positive finite difference so action scaling
       cannot divide by zero or overflow.
   * - Shape
     - Empty means a vector. Otherwise every dimension must be positive and
       their product must equal the number of stored bounds. Invalid or
       overflowing shapes are rejected without constructing sample points.

Dictionary, discrete, text, base ``FSpace``, and custom space types are rejected
by this initial policy. Unsupported types need an explicit extension and
learner agreement before use. This is structural validation; a native subclass
must also check its compiled learner's dimensions and preserve the agreed
preprocessing, units and action scaling.

Schola's existing ``FSpace::Validate`` / ``Contains`` methods check actual point
values against a space. They do not validate the space definition itself. This
policy checks definitions; decision requests and results also reuse existing
point validation, with additional checks for finite values and explicit shapes.
Experience submission is currently provided by the SAC subclass, not this base.

Initialize and inspect the policy
--------------------------------------------------

In Blueprint, construct an Adaptive NPC Policy, retain it in an owning variable,
and call ``Init`` with an interaction definition. Check the return value and
read ``GetLastError`` if it fails. ``GetInteractionDefinition`` returns an
independent copy of the accepted definitions; before initialization it returns
false without changing the output.

In C++, keep the policy alive with a normal Unreal-owned reference, for example
this member of the owning actor or UObject:

.. code-block:: cpp

   UPROPERTY()
   TObjectPtr<UAdaptiveNPCPolicy> AdaptivePolicy;

Then initialize it from that object's code:

.. code-block:: cpp

   #include "Policies/AdaptiveNPCPolicy.h"
   #include "Spaces/BoxSpace.h"

   TInstancedStruct<FSpace> Observations, Actions;
   Observations.InitializeAs<FBoxSpace>(TArray<FBoxSpaceDimension>{
       FBoxSpaceDimension::Unbounded(), FBoxSpaceDimension::Unbounded()});
   Actions.InitializeAs<FBoxSpace>(TArray<FBoxSpaceDimension>{
       FBoxSpaceDimension(-1.0f, 1.0f), FBoxSpaceDimension(-1.0f, 1.0f)});

   AdaptivePolicy = NewObject<UAdaptiveNPCPolicy>(this);
   if (!AdaptivePolicy->Init(FInteractionDefinition(Observations, Actions)))
   {
       UE_LOG(LogTemp, Warning, TEXT("%s"), *AdaptivePolicy->GetLastError());
       return;
   }
   // Base validation succeeded. This base object has no learner or Ready session.

A new policy starts ``Uninitialized``. A first invalid initialization reports
``Failed`` and remains uninitialized. A valid initialization reports
``Initialized`` (displayed as **Locally Initialized** in Blueprint), clears the
previous error, and owns a copy of the definition. Mutating the caller's input
or a retrieved copy cannot change the policy's stored definition.

An invalid replacement returns false and records an error while preserving the
previous valid definition and initialized state. A later valid initialization
can replace it. Always check ``Init``'s return value when changing definitions;
``IsInitialized`` reports whether an accepted definition still exists.

Base initialization never reports ``Ready``. A native learner subclass calls
protected ``StartSession`` once its learner is ready; ``UOnlineSACPolicy`` does
this during its overridden ``Init``. ``Shutdown`` reports ``Stopped``.
``IsInferenceBusy`` reports pending, unexpired inference only.
``Think`` and ``BatchedThink`` return false promptly, direct callers to the
request/result API, and preserve the action or whole action array supplied by
the caller. See :doc:`adaptive_decision_lifecycle` for the implemented rules.

Verification
------------

The Unreal automation tests under ``Schola.Inference.Adaptive`` cover supported
definitions, malformed Blueprint payloads, bounds and shapes, recovery after
invalid initialization, owned copies, output preservation, and Blueprint
exposure. Run these alongside ``Schola.Inference.Paradigms`` to check the
existing mock API. These tests establish the base foundation. Native SAC
integration tests live in the AdaptiveSAC host and must be run separately.
