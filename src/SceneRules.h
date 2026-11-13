#pragma once

#include "NodeRules.h"

#include <cmath>
#include <vector>

namespace tds::progression {

enum Counter : unsigned { N, M, F, C, S, A, V, VG, AN, OR, CounterCount };
using Counters = std::array<int, CounterCount>;

// QA composer constraints only. Real provider deltas can have other shapes.
[[nodiscard]] constexpr bool validMockScene(const Counters& delta)
{
    if (std::ranges::any_of(delta, [](int value) { return value < 0; })) return false;
    if (delta[N]>1 || delta[S]>1 || delta[N]+delta[S]!=1) return false;
    if (delta[A]>1 || delta[V]>1 || (delta[A] && delta[V])) return false;
    const bool hasPartner=delta[M] || delta[F] || delta[C];
    if (delta[N] ? !hasPartner : (hasPartner || delta[A] || delta[V])) return false;
    return delta[VG] || delta[AN] || delta[OR];
}


struct SceneAward {
    std::int64_t satisfaction{};
    std::int64_t corruption{};
    int soloProgress{};  // S increases since reset, capped at 12.
    int durationHours{};
    std::optional<std::int64_t> actualBlock;
    std::optional<int> effectiveChange;
    std::optional<std::int64_t> soloBaseK;
    std::optional<std::int64_t> soloExtraK;
};

constexpr int soloSatisfaction(int n)
{
    if (n<=12) return 24-n;
    if (n<=50) return -2;
    if (n<=100) return -4;
    if (n<200) return -6;
    return -8;
}

constexpr int soloCorruption(int n)
{
    if (n<=12) return 2*n+2;
    if (n<=50) return 28;
    if (n<=100) return 32;
    if (n<200) return 36;
    return 40;
}

inline constexpr std::array karmaFirst{2,1,1,3,0,5,5,1,6,1};
inline constexpr std::array karmaFinal{36,36,36,42,0,42,54,36,60,36};

[[nodiscard]] constexpr std::int64_t karmaNumerator11(unsigned counter, int before, int after)
{
    std::int64_t result=0;
    for (int t=std::min(before,12)+1; t<=std::min(after,12); ++t) {
        result+=11*karmaFirst[counter]+(karmaFinal[counter]-karmaFirst[counter])*(t-1);
    }
    const auto tail=static_cast<std::int64_t>(after)-std::max(before,12);
    return result+std::max<std::int64_t>(0,tail)*11*karmaFinal[counter];
}

[[nodiscard]] constexpr std::int64_t soloRampSum(int progress, int delta)
{
    const int ramp=std::min(delta,12-progress);
    return std::int64_t{ramp}*(2*progress+ramp+1)/2+std::int64_t{delta-ramp}*12;
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
    std::array<int,CounterCount> kWeights{};
    if (nodes.active(Node::Anal2)) { xWeights[AN]=7; kWeights[AN]+=2; }
    else if (nodes.active(Node::Anal1)) { xWeights[AN]=1; kWeights[AN]+=1; }
    if (nodes.active(Node::Creature2)) { xWeights[C]=11; kWeights[C]+=4; }
    else if (nodes.active(Node::Creature1)) { xWeights[C]=1; kWeights[C]+=2; }
    if (nodes.active(Node::Victim2)) { xWeights[V]=15; kWeights[V]+=6; }
    else if (nodes.active(Node::Victim1)) { xWeights[V]=1; kWeights[V]+=3; }
    if (nodes.active(Node::Solitude)) { xWeights[S]+=3; xWeights[N]-=2; }
    if (nodes.active(Node::Affinity)) { xWeights[N]+=3; xWeights[S]-=2; kWeights[N]-=1; kWeights[S]+=1; }
    if (nodes.active(Node::Dominance)) { xWeights[A]+=4; xWeights[V]-=4; }

    constexpr std::array purityBonus{20,15,10,5,0};
    const int bonus=purityBonus[purityTier(corruption)];
    std::int64_t xHundredths=0,k11=0;
    for (unsigned i=0;i<delta.size();++i) {
        xHundredths+=std::int64_t{delta[i]}*xWeights[i]*(xWeights[i]>0?100+bonus:100);
        if (i!=S) k11+=std::max<std::int64_t>(0,karmaNumerator11(i,before[i],after[i])+std::int64_t{delta[i]}*kWeights[i]*11);
    }
    const auto ramp=soloRampSum(soloProgress,delta[S]);
    const std::int64_t soloBase=after[N]==0 ? 3*ramp : std::int64_t{delta[S]}*soloCorruption(after[N]);
    const std::int64_t soloExtra=4*ramp;
    k11+=std::max<std::int64_t>(0,soloBase+soloExtra+std::int64_t{delta[S]}*kWeights[S])*11;
    soloProgress+=std::min(delta[S],12-soloProgress);
    if (nodes.active(Node::Erosion)) {
        xHundredths-=300;
        k11=k11*8;
    }
    const int duration=nodes.active(Node::LongEcho)?16:nodes.active(Node::Passion)?4:8;
    SceneAward result{xHundredths/100,k11/(nodes.active(Node::Erosion)?55:11),soloProgress,duration};
    result.soloBaseK=soloBase;
    result.soloExtraK=soloExtra;
    return result;
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

    // Remove the positive net without changing remaining block deadlines.
    std::int64_t consumePositiveNet() {
        auto remaining=std::max<std::int64_t>(0,total());
        const auto consumed=remaining;
        for (auto& block:blocks_) if (block.value>0 && remaining>0) {
            const auto take=std::min(block.value,remaining);
            block.value-=take; remaining-=take;
        }
        std::erase_if(blocks_,[](const auto& block){return block.value==0;});
        return consumed;
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