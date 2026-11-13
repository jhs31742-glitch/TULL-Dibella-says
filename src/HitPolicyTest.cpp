#include "HitPolicy.h"
#include "Phase1.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace tds;
    constexpr std::uint32_t ironSword = 0x12eb7;
    static_assert(rejectWeaponOrigin(true, 0, ironSword, ironSword, false) == nullptr);
    static_assert(rejectWeaponOrigin(false, 0, ironSword, ironSword, false) != nullptr);
    static_assert(rejectWeaponOrigin(true, 1, ironSword, ironSword, false) != nullptr);
    static_assert(rejectWeaponOrigin(true, 0, 0, ironSword, false) != nullptr);
    static_assert(rejectWeaponOrigin(true, 0, 7, ironSword, false) != nullptr);
    static_assert(rejectWeaponOrigin(true, 0, ironSword, ironSword, true) != nullptr);
    static_assert(attackHand(false, true, true, true) == Hand::Right);
    static_assert(attackHand(true, true, true, true) == Hand::Left);
    static_assert(attackHand(false, false, true, true) == Hand::Ambiguous);
    static_assert(attackHand(false, false, false, true) == Hand::Left);
    static_assert(attackHand(false, true, false, true) == Hand::Mismatch);
    static_assert(attackHand(true, false, true, false) == Hand::Mismatch);
    static_assert(attackHand(false, false, false, false) == Hand::Mismatch);
    const auto target = calculate(Group::WeaponGrowth, 155);
    if (target.stage != 31 || target.weapon.flatDamage != 23 ||
        target.weapon.damagePercent != 12 || target.weapon.lifeSteal != 3) return 1;
    const float physical = 97.246f;
    const float expected = physical + target.weapon.flatDamage +
        physical * target.weapon.damagePercent * 0.01f;
    if (std::abs(expected - 131.91552f) > 0.0001f) return 1;
    std::cout << "PASS 13 origin/hand cases, stage 31 fixed/percent/steal and reference arithmetic\n";
}
