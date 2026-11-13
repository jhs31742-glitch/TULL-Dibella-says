#include "EngineRates.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace tds::progression;
    Investment nodes{};
    assert(engineRates(nodes,100,0).spellCostBasisPoints==-1500);
    assert(engineRates(nodes,30,0).spellCostBasisPoints==-600);
    assert(engineRates(nodes,-100,0).spellCostBasisPoints==1500);
    assert(engineRates(nodes,-100,899).spellCostBasisPoints==1800);
    assert(engineRates(nodes,-100,900).spellCostBasisPoints==2500);
    assert(engineRates(nodes,100,3000).spellCostBasisPoints==500);
    assert(engineRates(nodes,0,899).incomingDamageBasisPoints==0);
    assert(engineRates(nodes,0,900).incomingDamageBasisPoints==300);
    assert(engineRates(nodes,0,1400).incomingDamageBasisPoints==300);
    assert(engineRates(nodes,0,2000).incomingDamageBasisPoints==800);
    assert(engineRates(nodes,0,3000).incomingDamageBasisPoints==1500);
    nodes.enabled=true;
    assert(purchase(nodes,Node::SatisfactionResist,200));
    assert(purchase(nodes,Node::CorruptionResist,200));
    assert(engineRates(nodes,-100,0).spellCostBasisPoints==0);
    assert(engineRates(nodes,-100,100).spellCostBasisPoints==100);
    assert(engineRates(nodes,-100,3000).spellCostBasisPoints==1000);
    assert(engineRates(nodes,-100,3000).incomingDamageBasisPoints==370);
    nodes.enabled=false;
    assert(engineRates(nodes,-100,3000).spellCostBasisPoints==4000);
    assert(engineRates(nodes,-100,3000).incomingDamageBasisPoints==1500);
    nodes.enabled=true;
    assert(engineRates(nodes,-100,3000).incomingDamageBasisPoints==370);
    Investment erosion{};
    erosion.enabled=true;
    assert(purchase(erosion,Node::Erosion,200));
    assert(engineRates(erosion,0,3000).spellCostBasisPoints==4000);
    assert(engineRates(erosion,0,3000).incomingDamageBasisPoints==3000);
    assert(engineRates(erosion,0,900).incomingDamageBasisPoints==600);
    std::cout << "PASS engine rate tiers, additive composition, resistance, erosion, OFF and reactivation\n";
}
