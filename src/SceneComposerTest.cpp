#include "ProgressionControl.h"
#include <cassert>
#include <iostream>
using namespace tds::progression;
int main()
{
    ProgressionControl qa;
    qa.enterDeveloperMode();
    const Counters multi{1,2,0,0,0,0,1,1,0,1};
    const Counters solo{0,0,0,0,1,0,0,1,0,0};
    assert(validMockScene(multi) && validMockScene(solo));
    for (const Counters bad : {Counters{}, Counters{1,0,0,0,0,0,0,1,0,0},
        Counters{1,1,0,0,1,0,0,1,0,0}, Counters{1,1,0,0,0,1,1,1,0,0},
        Counters{2,1,0,0,0,0,0,1,0,0}, Counters{1,1,0,0,0,0,0,0,0,0}})
        assert(!validMockScene(bad));
    assert(qa.mockScene(multi) && qa.mockScene(multi));
    assert(qa.effectiveCount(std::nullopt)==2);
    assert((*qa.current().stats)[M]==4 && (*qa.current().stats)[VG]==2);
    assert(qa.current().corruption==14 && qa.current().recentBlocks[1].has_value());
    assert(qa.mockScene(solo));
    assert(qa.effectiveCount(std::nullopt)==2 && (*qa.current().stats)[S]==1);
    assert(!qa.beginRealRead() && !qa.stateForSave());
    std::cout << "PASS native scene rules, provider state, X/K and QA isolation\n";
}
