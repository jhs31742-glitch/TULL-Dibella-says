#pragma once

#include <cstdint>

namespace tds {

constexpr const char* rejectWeaponOrigin(bool armedMelee, std::uint32_t projectile,
    std::uint32_t source, std::uint32_t hitWeapon, bool attackExcluded)
{
    if (!armedMelee) return "NOT_ARMED_MELEE";
    if (projectile) return "PROJECTILE_HIT";
    if (!source || source != hitWeapon) return "SOURCE_WEAPON_MISMATCH";
    if (attackExcluded) return "ATTACK_DATA_EXCLUDED";
    return nullptr;
}

enum class Hand { Right, Left, Mismatch, Ambiguous };

constexpr Hand attackHand(bool leftEvidence, bool attackDataPresent, bool rightMatches, bool leftMatches)
{
    if (leftEvidence) return leftMatches ? Hand::Left : Hand::Mismatch;
    if (attackDataPresent) return rightMatches ? Hand::Right : Hand::Mismatch;
    if (rightMatches && leftMatches) return Hand::Ambiguous;
    if (rightMatches) return Hand::Right;
    return leftMatches ? Hand::Left : Hand::Mismatch;
}

}  // namespace tds
