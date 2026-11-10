#pragma once

#include <cmath>
#include <optional>

#include "ProgressionRules.h"

namespace tds::progression {

struct DestructionDamageBonus {
    int purityPercent{};
    int corruptionPercent{};
};

[[nodiscard]] constexpr DestructionDamageBonus destructionDamageBonus(
    std::optional<int> count, std::int64_t corruption)
{
    if (!count || *count<0) return {};
    const auto tier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(
        corruption,0,corruptionThresholds.back())));
    constexpr int finalTier=static_cast<int>(corruptionThresholds.size())-1;
    return {*count==0 ? 50 : 0,(tier*30+finalTier/2)/finalTier};
}

// Facts supplied by a validated runtime adapter. Unknown source or scope
// must stay false. This policy never changes the underlying spell record.
struct DestructionDamageContext {
    bool playerCaster{};
    bool distinctTarget{};
    bool destructionSpell{};
    bool directImpact{};
    bool healthDamage{};
    bool secondary{};
};

[[nodiscard]] inline std::optional<float> destructionDamage(
    const DestructionDamageContext& hit, float incomingDamage, DestructionDamageBonus bonus)
{
    if (!hit.playerCaster || !hit.distinctTarget || !hit.destructionSpell ||
        !hit.directImpact || !hit.healthDamage || hit.secondary ||
        !std::isfinite(incomingDamage) || incomingDamage<=0.0f ||
        bonus.purityPercent<0 || bonus.corruptionPercent<0) return std::nullopt;

    const auto percent=static_cast<double>(bonus.purityPercent)+bonus.corruptionPercent;
    const auto scaled=static_cast<float>(incomingDamage*(1.0+percent/100.0));
    if (!std::isfinite(scaled)) return std::nullopt;
    return scaled;
}

}  // namespace tds::progression
