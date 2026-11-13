#include "ProgressionControl.h"
#include "MeleeGrowth.h"
#include "DeveloperGate.h"
#include <cassert>
#include <iostream>
using namespace tds::progression;
int main(){
assert(developerPasswordAccepted("TULL")&&developerPasswordAccepted("tull")&&developerPasswordAccepted("TuLl"));
assert(!developerPasswordAccepted("")&&!developerPasswordAccepted("TULL ")&&!developerPasswordAccepted("BAD"));
Investment owned;owned.purchased[index(Node::Recovery1)]=true;
assert(offeringGold(owned,80)==0);owned.purchased[index(Node::Recovery2)]=true;
assert(offeringGold(owned,80)==80);owned.purchased[index(Node::Recovery3)]=true;
assert(offeringGold(owned,80)==160&&offeringGold(owned,-80)==0&&offeringGold(owned,150)==200);
owned.enabled=false;assert(offeringGold(owned,80)==0);
int cases=0;
for(int gear=0;gear<=50;++gear)for(int growth=-40;growth<=40;++growth){
const auto c=planMeleeCommit(float(gear),0,{float(growth),0},true);
assert(c.damage==float(std::max(0,gear+growth)));assert(c.pendingReduction==0);++cases;
}
ProgressionControl c;assert(!c.resetMockStatistics());c.enterDeveloperMode();Counters stats{};stats[N]=200;assert(c.setMockCounters(stats));
c.setNodesEnabled(true);assert(c.buy(Node::Recovery1));assert(c.current().nodes.spent()==2);
for(int x:{1,2,79,80,99,100}){
assert(c.setMockSatisfaction(x));const auto gain=c.offerSatisfaction();assert(gain==x/2);assert(c.current().satisfaction.total()==0);
}
for(int x:{-100,-1,0}){assert(c.setMockSatisfaction(x));const auto k=c.current().corruption;assert(c.offerSatisfaction()==-1);assert(c.current().satisfaction.total()==x&&c.current().corruption==k);}
c.setNodesEnabled(false);assert(c.setMockSatisfaction(80));assert(c.offerSatisfaction()==-1);assert(c.current().satisfaction.total()==80);
c.setNodesEnabled(true);assert(c.offerSatisfaction()==40);assert(c.current().satisfaction.records().empty());
assert(c.setMockCounters(Counters{}));assert(c.current().stats==Counters{});assert(c.offerSatisfaction()==-1);assert(!c.stateForSave());
assert(c.setMockSatisfaction(80));assert(c.setMockCorruption(45000));
assert(c.resetMockStatistics());assert(c.current().stats==Counters{}&&c.current().satisfaction.total()==0&&c.current().corruption==0&&c.current().nodes.spent()==0);
SatisfactionBlocks blocks;assert(blocks.restore({80,8}));assert(blocks.restore({-10,9}));assert(blocks.consumePositiveNet()==70);assert(blocks.total()==0);assert(blocks.records()[0].expiresAt==8&&blocks.records()[1].expiresAt==9);
blocks.expire(8);assert(blocks.total()==-10);
std::cout<<"PASS "<<cases<<" melee pool cases; offering, password, QA isolation, unchanged deadlines"<<std::endl;
}
