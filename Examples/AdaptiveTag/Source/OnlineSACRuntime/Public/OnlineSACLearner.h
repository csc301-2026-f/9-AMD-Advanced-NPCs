// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
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

class ONLINESACRUNTIME_API OnlineSACLearner {
public:
    static constexpr unsigned ObservationSize = ObservationDim;
    static constexpr unsigned ActionSize = ActionDim;
    explicit OnlineSACLearner(const OnlineSACConfig& config = {});
    ~OnlineSACLearner();
    OnlineSACLearner(const OnlineSACLearner&) = delete;
    OnlineSACLearner& operator=(const OnlineSACLearner&) = delete;

    // Any thread. Validates sizes, finiteness and the [-1, 1] action range; invalid input changes nothing but the rejected count.
    AddResult AddExperience(const float* observation, unsigned observationSize, const float* action, unsigned actionSize, float reward,
        const float* nextObservation, unsigned nextObservationSize, bool terminated, bool truncated);
    // Any thread. Samples from the current snapshot, or returns its mean when deterministic or frozen.
    // Returns false (and leaves `action` untouched) for invalid input.
    bool Act(const float* observation, unsigned observationSize, float* action, unsigned actionSize, bool deterministic = false, unsigned* version = nullptr) const;
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
}
