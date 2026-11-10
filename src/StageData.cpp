#include "Phase1.h"
#include <iostream>

int main()
{
    for (int stage = 1; stage <= 40; ++stage) {
        const int count = tds::thresholds[stage - 1];
        const auto w = tds::calculate(tds::Group::WeaponGrowth, count).weapon;
        const auto a = tds::calculate(tds::Group::AccessoryGrowth, count).accessory;
        std::cout << stage << ',' << w.health << ',' << w.stamina << ',' << a.armor
                  << ',' << a.magicka << ',' << a.stamina << ',' << a.disease << ','
                  << a.poison << ',' << a.magic << ',' << a.fire << ',' << a.frost
                  << ',' << a.shock << ',' << a.spellAbsorption << '\n';
    }
}
