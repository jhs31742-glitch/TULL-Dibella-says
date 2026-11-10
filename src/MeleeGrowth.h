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
    const auto tier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(karma,0,3000)));
    const int rate=satisfactionEffectPercent(appliedSatisfaction(x));
    float satisfaction=rate * (rate<0 ? 0.15f : 0.12f);
    if (rate<0) {
        constexpr std::array residual{0.75f,0.80f,0.85f,0.90f,1.0f};
        satisfaction*=residual[std::min(tier,4)];
        if (nodes.active(Node::SatisfactionResist)) satisfaction*=0.25f;
    }
    const int karmaFlat=(tier>=3 ? 3 : 0)+(tier>=7 ? 5 : 0);
    return {satisfaction+karmaFlat+passive.flatWeaponDamage+(count==0 ? 30 : 0),
        passive.healthStealOnValidHit};
}

// The legacy post-hit seam can add damage and heal the attacker. A negative
// residual would require a verified seam before the engine commits damage.
struct MeleeCommit {
    float damage{};
    float pendingReduction{};
    int steal{};
};

[[nodiscard]] constexpr MeleeCommit planMeleeCommit(
    float gearDamage, int gearSteal, MeleeGrowth growth, bool observedHit)
{
    const float flat=gearDamage+(observedHit ? growth.flat : 0.0f);
    return {std::max(0.0f,flat),std::max(0.0f,-flat),
        gearSteal+(observedHit ? growth.steal : 0)};
}

}  // namespace tds::progression