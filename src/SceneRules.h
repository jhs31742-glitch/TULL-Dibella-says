#pragma once

#include "NodeRules.h"

#include <cmath>
#include <vector>

namespace tds::progression {

enum Counter : unsigned { N, M, F, C, S, A, V, VG, AN, OR, CounterCount };
using Counters = std::array<int, CounterCount>;

struct SceneAward {
    std::int64_t satisfaction{};
    std::int64_t corruption{};
    int soloProgress{};  // N=0 ramp progress, capped at the final step.
    int durationHours{};
};

constexpr int soloSatisfaction(int n)
{
    if (n==0) return 10;
    if (n<=12) return 8;
    if (n<=50) return 6;
    if (n<=150) return 4;
    if (n<=500) return 2;
    return 1;
}

[[nodiscard]] constexpr std::optional<SceneAward> sceneAward(
    const Counters& before, const Counters& after, std::int64_t corruption,
    int soloProgress, const Investment& nodes)
{
    if (soloProgress<0 || soloProgress>12 || corruption<0) return std::nullopt;
    Counters delta{};
    for (unsigned i=0;i<delta.size();++i) {
        if (before[i]<0 || after[i]<before[i]) return std::nullopt;
        delta[i]=after[i]-before[i];
    }
    if (before==after) return SceneAward{0,0,soloProgress,0};

    std::array xWeights{6,2,2,-11,soloSatisfaction(after[N]),5,-15,4,-7,2};
    const std::array kWeights{2,1,1,3,1,5,1,1,2,1};
    if (nodes.active(Node::Anal2)) xWeights[AN]=7;
    else if (nodes.active(Node::Anal1)) xWeights[AN]=1;
    if (nodes.active(Node::Creature2)) xWeights[C]=11;
    else if (nodes.active(Node::Creature1)) xWeights[C]=1;
    if (nodes.active(Node::Victim2)) xWeights[V]=15;
    else if (nodes.active(Node::Victim1)) xWeights[V]=1;
    if (nodes.active(Node::Solitude)) { xWeights[S]+=3; xWeights[N]-=2; }
    if (nodes.active(Node::Affinity)) { xWeights[N]+=3; xWeights[S]-=2; }
    if (nodes.active(Node::Dominance)) { xWeights[A]+=4; xWeights[V]-=4; }

    std::int64_t x=0,k=0;
    for (unsigned i=0;i<delta.size();++i) {
        x+=std::int64_t{delta[i]}*xWeights[i];
        k+=std::int64_t{delta[i]}*kWeights[i];
    }
    if (after[N]==0) {
        const int ramp=std::min(delta[S],12-soloProgress);
        k-=delta[S];
        k+=std::int64_t{ramp}*(2*soloProgress+ramp+1)/2;
        k+=std::int64_t{delta[S]-ramp}*12;
        soloProgress+=ramp;
    }
    const int bonus=corruption<100?20:corruption<250?15:corruption<500?10:corruption<900?5:0;
    std::int64_t xHundredths=x*(x>0?100+bonus:100);
    if (nodes.active(Node::Erosion)) {
        xHundredths-=300;
        k=k*16/10;
    }
    const int duration=nodes.active(Node::LongEcho)?16:nodes.active(Node::Passion)?4:8;
    return SceneAward{xHundredths/100,k,soloProgress,duration};
}

struct SatisfactionBlock {
    std::int64_t value{};
    double expiresAt{};
};

class SatisfactionBlocks {
public:
    void expire(double gameHours)
    {
        if (!std::isfinite(gameHours)) return;
        std::erase_if(blocks_,[gameHours](const auto& block){return block.expiresAt<=gameHours;});
    }

    [[nodiscard]] std::int64_t total() const
    {
        return std::accumulate(blocks_.begin(),blocks_.end(),std::int64_t{0},
            [](auto sum,const auto& block){return sum+block.value;});
    }

    bool grant(const SceneAward& award,double gameHours)
    {
        if (!std::isfinite(gameHours) || award.durationHours<4 || award.satisfaction==0) return false;
        expire(gameHours);
        const auto current=total();
        if ((current>=100&&award.satisfaction>0)||(current<=-100&&award.satisfaction<0)) return false;
        blocks_.push_back({award.satisfaction,gameHours+award.durationHours});
        return true;
    }

    [[nodiscard]] const std::vector<SatisfactionBlock>& records() const { return blocks_; }

    bool restore(SatisfactionBlock block)
    {
        // Bounds cover the largest award possible from signed 32-bit counters.
        constexpr std::int64_t limit = 1'000'000'000'000;
        if (!std::isfinite(block.expiresAt) || block.expiresAt < 0 ||
            block.value == 0 || block.value < -limit || block.value > limit) return false;
        blocks_.push_back(block);
        return true;
    }

private:
    std::vector<SatisfactionBlock> blocks_;
};

}  // namespace tds::progression