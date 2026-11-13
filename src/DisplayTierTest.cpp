#include "EngineRates.h"
#include "SpellDamagePolicy.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace tds::progression;
int main()
{
    assert(corruptionAuraSlot(-1)==1);
    assert(corruptionAuraSlot(0)==1);
    assert(corruptionAuraSlot(499)==1);
    assert(corruptionAuraSlot(500)==2);
    assert(corruptionAuraSlot(1499)==2);
    assert(corruptionAuraSlot(1500)==2);
    assert(corruptionAuraSlot(3999)==2);
    assert(corruptionAuraSlot(4000)==3);
    assert(corruptionAuraSlot(21000)==6);
    assert(corruptionAuraSlot(30000)==7);
    assert(corruptionAuraSlot(45000)==8);
    assert(corruptionAuraSlot(std::numeric_limits<std::int64_t>::max())==8);
    assert(corruptionTier(500)==0 && corruptionTier(1500)==1);
    assert(destructionDamageBonus(1,500).corruptionPercent==0);
    assert(destructionDamageBonus(1,1500).corruptionPercent==4);
    assert(destructionDamageBonus(1,45000).corruptionPercent==30);
    assert(engineRates({},0,45000).incomingDamageBasisPoints==1500);
    std::cout<<"PASS curiosity aura mapping, no unintended AV or combat tier shift"<<std::endl;
}
