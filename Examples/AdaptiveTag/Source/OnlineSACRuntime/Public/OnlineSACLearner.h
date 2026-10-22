// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "ChaseConfig.h"
#include "ChaseTask.h"
#include <cstdint>
#include <memory>

namespace online_sac {
struct OnlineSACConfig {
    unsigned seed = 1;
    unsigned queueCapacity = 4096;
    unsigned updateEvery = 50;
    unsigned warmupExperiences = 1000;
    bool frozen = false;
};
enum class AddResult { Accepted, Rejected, Dropped, Frozen };
struct OnlineSACStats {
    uint64_t accepted = 0, rejected = 0, dropped = 0, ignoredFrozen = 0;
    uint64_t stored = 0, updates = 0;
    unsigned version = 0, rounds = 0, queued = 0;
    bool training = false, running = false, frozen = false;
    double lastRoundMs = 0;
};

// Push learner for one observation size (compile time for RLtools). The action size is ActionDim (2).
// Only the sizes instantiated in Private/OnlineSACRuntime.cpp exist: ObservationDim (chase v1) and ChaseObservationSize (chase v2).
template <unsigned ObservationDimension>
class ONLINESACRUNTIME_API BasicOnlineSACLearner {
public:
    static constexpr unsigned ObservationSize = ObservationDimension;
    static constexpr unsigned ActionSize = ActionDim;
    // One published actor version. Immutable, so it can be kept (e.g. as a snapshot) and used from any thread.
    struct Policy;
    explicit BasicOnlineSACLearner(const OnlineSACConfig& config = {});
    ~BasicOnlineSACLearner();
    BasicOnlineSACLearner(const BasicOnlineSACLearner&) = delete;
    BasicOnlineSACLearner& operator=(const BasicOnlineSACLearner&) = delete;

    // Any thread. Validates sizes, finiteness and the [-1, 1] action range; invalid input changes nothing but the rejected count.
    AddResult AddExperience(const float* observation, unsigned observationSize, const float* action, unsigned actionSize, float reward,
        const float* nextObservation, unsigned nextObservationSize, bool terminated, bool truncated);
    // Any thread. Samples from the current snapshot, or returns its mean when deterministic or frozen.
    // Returns false (and leaves `action` untouched) for invalid input.
    bool Act(const float* observation, unsigned observationSize, float* action, unsigned actionSize, bool deterministic = false, unsigned* version = nullptr) const;
    // Any thread. The currently published policy (never null).
    std::shared_ptr<const Policy> CurrentPolicy() const;
    // Deterministic (mean) action of `policy`. Touches no learner state, so it never changes training.
    static bool ActWith(const Policy& policy, const float* observation, unsigned observationSize, float* action, unsigned actionSize);
    static unsigned VersionOf(const Policy& policy);
    void SetFrozen(bool frozen);
    unsigned Version() const;
    OnlineSACStats ReadStats() const;
    bool WaitForLearner(unsigned maxBacklog, unsigned timeoutMs);
    // Stops the learner thread (abandoning a round in progress) and joins it. Safe to call more than once.
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> data;
};

static_assert(ChaseActionSize == ActionDim, "chase v2 uses the same action size as the learner");
// Members are defined only in Private/OnlineSACLearner.inl and explicitly instantiated there for these two sizes.
using OnlineSACLearner = BasicOnlineSACLearner<ObservationDim>;  // chase v1 (6 observations)
using ChaseLearner = BasicOnlineSACLearner<ChaseObservationSize>; // chase v2 (8 observations)
}
