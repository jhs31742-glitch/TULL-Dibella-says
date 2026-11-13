#pragma once

#include "NodeRules.h"

namespace tds::progression {

struct MeleeGrowth {
    float flat{};
    int steal{};
};

[[nodiscard]] constexpr MeleeGrowth meleeGrowth(
    const Investment& nodes, int count, std::int64_t x, std::int64_t karma)
{
    if (count<0) return {};
    const auto passive=passiveEffects(nodes,count);
    const auto tier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(karma,0,corruptionThresholds.back())));
    const int rate=satisfactionEffectPercent(appliedSatisfaction(x));
    constexpr std::array remaining{75,80,85,90,100};
    const int residual=rate<0 ? std::max(0,remaining[purityTier(karma)]-
        (nodes.active(Node::SatisfactionResist)?75:0)) : 100;
    const int satisfactionTenths=rate*(rate<0?15:12)*residual/1000;
    const int karmaFlat=(tier>=3 ? 3 : 0)+(tier>=7 ? 5 : 0);
    return {satisfactionTenths/10.0f+karmaFlat+passive.flatWeaponDamage+(count==0 ? 30 : 0),
        passive.healthStealOnValidHit};
}

// Signed adjustments only reduce the TDS additional pool; vanilla damage is untouched.
struct MeleeCommit {
    float damage{};
    float pendingReduction{};
    int steal{};
};

[[nodiscard]] constexpr MeleeCommit planMeleeCommit(
    float gearDamage, int gearSteal, MeleeGrowth growth, bool observedHit)
{
    const float flat=gearDamage+(observedHit ? growth.flat : 0.0f);
    return {std::max(0.0f,flat),0.0f,
        gearSteal+(observedHit ? growth.steal : 0)};
}

}  // namespace tds::progression