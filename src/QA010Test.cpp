#include "ProgressionControl.h"
#include "MeleeGrowth.h"
#include "SpellDamagePolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace tds::progression;
    ProgressionControl qa;
    qa.enterDeveloperMode();
    qa.expire(450.0);
    assert(qa.setMockSatisfaction(100));
    qa.expire(457.75);
    assert(qa.clockHours(457.75)==7.75 && qa.current().satisfaction.total()==100);
    qa.expire(458.0);
    assert(qa.current().satisfaction.total()==0);
    Counters sample{};
    sample[N]=200;
    assert(qa.setMockCounters(sample));
    assert(qa.effectiveCount(std::nullopt)==200);
    assert(passiveEffects(qa.current().nodes,200).baseResourceReductionPercent==90);
    assert(qa.setMockCorruption(3000));
    assert(destructionDamageBonus(qa.effectiveCount(std::nullopt),3000).corruptionPercent==30);
    sample[N]=0;
    assert(qa.setMockCounters(sample));
    assert(qa.effectiveCount(std::nullopt)==0);
    assert(qa.current().corruption==0);
    assert(destructionDamageBonus(qa.effectiveCount(std::nullopt),0).purityPercent==50);
    assert(meleeGrowth(qa.current().nodes,0,0,0).flat==30);
    assert(!qa.stateForSave());
    assert(qa.loadState({}));
    assert(!qa.developerMode() && qa.clockHours(458.0)==458.0);
    std::cout << "PASS wait expiry, QA reset to purity, resource penalty and save isolation model\n";
}
