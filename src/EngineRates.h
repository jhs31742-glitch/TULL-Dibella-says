#pragma once

#include "NodeRules.h"

namespace tds::progression {

struct EngineRates {
    int spellCostBasisPoints;
    int incomingDamageBasisPoints;
};

constexpr EngineRates engineRates(const Investment& nodes, std::int64_t x, std::int64_t k)
{
    const auto tier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(k,0,corruptionThresholds.back())));
    constexpr std::array karmaCost{0,0,0,0,500,1000,1500,2000};
    constexpr std::array karmaDamage{0,0,0,0,300,300,800,1500};
    const auto karmaScale=nodes.active(Node::Erosion) ? 8 : nodes.active(Node::CorruptionResist) ? 1 : 4;
    const auto rate=satisfactionEffectPercent(appliedSatisfaction(x));
    constexpr std::array residual{75,80,85,90,100};
    const auto remaining=std::max(0,residual[purityTier(k)]-
        (nodes.active(Node::SatisfactionResist) ? 75 : 0));
    const int xCost=rate<0 ? -rate*20*remaining/100 : -rate*15;
    return {(xCost+karmaCost[tier]*karmaScale/4)/10*10,karmaDamage[tier]*karmaScale/4/10*10};
}

}  // namespace tds::progression
