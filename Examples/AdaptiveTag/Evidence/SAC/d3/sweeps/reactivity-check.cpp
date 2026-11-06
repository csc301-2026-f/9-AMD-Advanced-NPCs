#include "ChaseWorld.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace online_sac;
struct Run { std::vector<Vec2> r; std::vector<float> d; std::vector<char> fleeing; };
Run Play(RunnerPattern p, unsigned seed, int mode) { // 0 stand, 1 greedy
    ChaseConfig c; ChaseRng rng(seed); ChaseWorld w; w.Reset(c, p, rng); Run out;
    for (int t = 0; t < 200 && !w.State().caught; ++t) {
        float o[10]; float a[2] = {0, 0}; w.Observe(o); if (mode == 1) GreedyChaseAction(o, a); w.Step(a);
        out.r.push_back(w.State().runner.position); out.d.push_back(w.State().distance); out.fleeing.push_back(w.State().distance < c.flee_radius);
    }
    return out;
}
int main() {
    for (RunnerPattern p : {RunnerPattern::Square, RunnerPattern::RightTurn, RunnerPattern::Clockwise, RunnerPattern::Corner}) {
        double diff = 0, turnSum = 0, turnAbs = 0; int n = 0, nt = 0; double straight = 0; int ns = 0, caught = 0, total = 0;
        for (unsigned seed = 1; seed <= 100; ++seed) {
            Run a = Play(p, seed, 0), b = Play(p, seed, 1); ++total;
            size_t m = std::min(a.r.size(), b.r.size()), k = std::min<size_t>(m, 60);
            for (size_t i = 0; i < k; ++i) { diff += std::hypot(a.r[i].x - b.r[i].x, a.r[i].y - b.r[i].y); ++n; }
            if (b.r.size() < 200) ++caught;
            // heading changes of the runner while fleeing from the greedy chaser
            for (size_t i = 2; i < b.r.size(); ++i) if (b.fleeing[i] && b.fleeing[i-1]) {
                Vec2 d1{b.r[i-1].x - b.r[i-2].x, b.r[i-1].y - b.r[i-2].y}, d2{b.r[i].x - b.r[i-1].x, b.r[i].y - b.r[i-1].y};
                float cross = d1.x * d2.y - d1.y * d2.x, dot = d1.x * d2.x + d1.y * d2.y;
                if (std::hypot(d1.x,d1.y) < 0.5 || std::hypot(d2.x,d2.y) < 0.5) continue;
                float ang = std::atan2(cross, dot); turnSum += ang; turnAbs += std::fabs(ang); ++nt;
            }
        }
        std::printf("%-10s avg runner position difference, stand vs greedy chaser (first 60 steps): %5.1f units | fleeing steps %d: mean signed turn %+0.3f rad/step (negative = right/clockwise), mean |turn| %0.3f | caught by greedy %d%%\n",
            PatternName(p), diff / n, nt, nt ? turnSum / nt : 0, nt ? turnAbs / nt : 0, caught);
    }
}
