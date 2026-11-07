// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "TagLearner.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
#include <string>

bool Wait(tag_learning::Learner& learner,unsigned count) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline) {
        auto stats=learner.ReadStats();
        if(stats.experiences>=count && !stats.training && stats.queued==0) {
            if(!stats.healthy) std::cout<<"Unhealthy learner at experience "<<count<<" mse="<<stats.criticMse<<" p="<<stats.expectedLeft<<std::endl;
            return stats.healthy;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout<<"Timed out waiting for experience "<<count<<std::endl;
    return false;
}

bool CompareLearningAndFrozen(unsigned seed) {
    constexpr unsigned PretrainRounds = 256;
    constexpr unsigned CompareRounds = 256;
    constexpr float Tolerance = 1e-6f;

    tag_learning::Learner adaptive(seed);
    tag_learning::Learner frozen(seed);

    // Runner randomness is independent of both learners.
    std::mt19937 runnerRandom(seed + 6000);
    std::uniform_real_distribution<float> draw(0.0f, 1.0f);

    // Train both learners identically against an 80%-left runner.
    for (unsigned round = 1; round <= PretrainRounds; ++round) {
        const auto a = adaptive.Choose();
        const auto f = frozen.Choose();

        if (!a.valid || !f.valid ||
            a.route != f.route ||
            a.version != f.version ||
            std::abs(a.leftProbability - f.leftProbability) > Tolerance) {
            std::cerr << "Pretraining mismatch: seed=" << seed
                      << " round=" << round << '\n';
            return false;
        }

        const int runner = draw(runnerRandom) < 0.8f ? -1 : 1;
        const float reward = a.route == runner ? 1.0f : -1.0f;

        // Execute both submissions before waiting for their updates.
        const bool submittedA =
            adaptive.Submit({a, reward, a.route, round});
        const bool submittedF =
            frozen.Submit({f, reward, f.route, round});

        if (!submittedA || !submittedF ||
            !Wait(adaptive, round) || !Wait(frozen, round)) {
            return false;
        }
    }

    // ReadStats applies the final pending snapshots while still enabled.
    const auto startA = adaptive.ReadStats();
    const auto startF = frozen.ReadStats();

    if (!startA.healthy || !startF.healthy ||
        startA.updates != PretrainRounds ||
        startF.updates != PretrainRounds ||
        startA.appliedVersion != startA.trainedVersion ||
        startF.appliedVersion != startF.trainedVersion ||
        startA.appliedVersion != startF.appliedVersion ||
        std::abs(startA.expectedLeft - startF.expectedLeft) > Tolerance) {
        std::cerr << "Starting policy mismatch: seed=" << seed << '\n';
        return false;
    }

    frozen.SetTrainingEnabled(false);

    std::ofstream csv(
        "learning-vs-frozen-" + std::to_string(seed) + ".csv"
    );

    if (!csv) {
        std::cerr << "Could not open comparison CSV\n";
        return false;
    }

    csv << "seed,branch,round,runner_route,actor_route,reward,"
           "p_left,policy_version,updates_after_round\n";

    unsigned adaptiveCatches = 0;
    unsigned frozenCatches = 0;

    // Switch to an 80%-right runner.
    for (unsigned round = 1; round <= CompareRounds; ++round) {
        const auto a = adaptive.Choose();
        const auto f = frozen.Choose();

        if (!a.valid || !f.valid) {
            return false;
        }

        // Both branches face exactly the same runner choice.
        const int runner = draw(runnerRandom) < 0.2f ? -1 : 1;

        const float rewardA = a.route == runner ? 1.0f : -1.0f;
        const float rewardF = f.route == runner ? 1.0f : -1.0f;

        adaptiveCatches += rewardA > 0;
        frozenCatches += rewardF > 0;

        // Only the adaptive branch receives training experiences.
        const unsigned total = PretrainRounds + round;

        if (!adaptive.Submit({a, rewardA, a.route, total}) ||
            !Wait(adaptive, total)) {
            return false;
        }

        const auto statsA = adaptive.ReadStats();
        const auto statsF = frozen.ReadStats();

        if (!statsA.healthy || !statsF.healthy ||
            statsA.updates != total ||
            statsF.enabled ||
            statsF.updates != startF.updates ||
            statsF.appliedVersion != startF.appliedVersion ||
            std::abs(statsF.expectedLeft - startF.expectedLeft) >
                Tolerance) {
            std::cerr << "Comparison invariant failed: seed=" << seed
                      << " round=" << round << '\n';
            return false;
        }

        // Probability and version belong to the decision BEFORE its update.
        csv << seed << ",adaptive," << round << ','
            << runner << ',' << a.route << ',' << rewardA << ','
            << a.leftProbability << ',' << a.version << ','
            << statsA.updates << '\n';

        csv << seed << ",frozen," << round << ','
            << runner << ',' << f.route << ',' << rewardF << ','
            << f.leftProbability << ',' << f.version << ','
            << statsF.updates << '\n';
    }

    csv.flush();
    if (!csv) {
        std::cerr << "Could not finish writing comparison CSV\n";
        return false;
    }

    std::cout << "Comparison seed=" << seed
              << " starting_p_left=" << startF.expectedLeft
              << " adaptive_catch_rate="
              << double(adaptiveCatches) / CompareRounds
              << " frozen_catch_rate="
              << double(frozenCatches) / CompareRounds
              << '\n';

    return true;
}
int main(int argc,char** argv) {
    std::ofstream curve(argc>1?argv[1]:"learning-curve.csv");
    curve<<"seed,phase,round,expected_left,centre_left,updates,critic_mse,reward\n";
    bool passed=true;
    for(unsigned seed : {0u,1u,2u,2026u}) {
        tag_learning::Learner learner(seed);
        std::mt19937 random(seed+6000);
        std::uniform_real_distribution<float> draw(0,1);
        unsigned count=0;
        for(unsigned phase=0;phase<2;++phase) {
            float lastProbability=0;
            unsigned catches=0;
            for(unsigned round=0;round<256;++round) {
                auto decision=learner.Choose();
                const int route=decision.route;
                const int runner=draw(random)<(phase==0?0.8f:0.2f) ? -1:1;
                const float reward=route==runner?1.0f:-1.0f;
                passed=passed && decision.valid && decision.observation==1 && (route==-1 || route==1)
                    && decision.leftProbability>=0 && decision.leftProbability<=1
                    && std::abs(decision.chosenProbability-(route<0?decision.leftProbability:1-decision.leftProbability))<1e-5f;
                tag_learning::Experience experience{decision,reward,route,++count};
                if(!learner.Submit(experience) || !Wait(learner,count))return 2;
                const auto stats=learner.ReadStats();
                passed=passed && stats.updates==count;
                curve<<seed<<','<<phase<<','<<round+1<<','<<stats.expectedLeft<<','<<stats.centreLeft<<','<<stats.updates<<','<<stats.criticMse<<','<<reward<<'\n';
                if(round>=192) {lastProbability+=stats.expectedLeft/64; catches+=reward>0;}
            }
            bool learned=phase==0?lastProbability>0.65f:lastProbability<0.35f;
            std::cout<<"seed="<<seed<<" phase="<<phase<<" mean_p_left_last64="<<lastProbability<<" catches_last64="<<catches<<" learned="<<learned<<'\n';
            passed=passed && learned;
        }
        const auto before=learner.ReadStats();
        learner.SetTrainingEnabled(false);
        auto decision=learner.Choose();
        passed=passed && !learner.Submit({decision,1,-1,999});
        unsigned leftSamples=0;
        for(int i=0;i<4096;++i)leftSamples+=learner.Choose().route<0;
        const auto after=learner.ReadStats();
        passed=passed && before.updates==after.updates && before.expectedLeft==after.expectedLeft;
        passed=passed && std::abs(float(leftSamples)/4096-after.expectedLeft)<0.025f;
        learner.SetTrainingEnabled(true);
        auto invalid=tag_learning::Experience{decision,std::numeric_limits<float>::quiet_NaN(),-1,999};
        passed=passed && !learner.Submit(invalid) && learner.ReadStats().rejected==1;
        learner.Stop();
    }
    { tag_learning::Learner cancelled; cancelled.Stop(); }
    for(float reward : {-1.0f,1.0f}) {
        tag_learning::Learner initial(77);
        auto decision=initial.Choose();
        passed=passed && decision.valid && std::abs(decision.leftProbability-0.5f)<1e-6f;
        passed=passed && initial.Submit({decision,reward,decision.route,1}) && Wait(initial,1);
        passed=passed && initial.ReadStats().updates==1; // Actor acts and learns from round one.
        const float pLeft=initial.ReadStats().expectedLeft;
        const float chosenNow=decision.route<0?pLeft:1-pLeft;
        passed=passed && (reward>0?chosenNow>0.5f:chosenNow<0.5f);
        auto mismatch=initial.Choose();
        passed=passed && !initial.Submit({mismatch,1,-mismatch.route,2});
    }
    for (unsigned seed : {0u, 1u, 2u, 2026u}) {
        const bool comparisonPassed = CompareLearningAndFrozen(seed);
        passed = comparisonPassed && passed;
    }
    std::cout<<"PASS="<<passed<<'\n';
    return passed?0:1;
}
