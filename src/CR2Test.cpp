#include "ProgressionControl.h"
#include "EngineRates.h"
#include "MeleeGrowth.h"
#include "SpellDamagePolicy.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace tds::progression;

std::int64_t oracle(unsigned slot,int before,int delta)
{
    std::int64_t total=0;
    for(int j=1;j<=delta;++j){
        const int t=std::min(before+j,12);
        total+=11*karmaFirst[slot]+(karmaFinal[slot]-karmaFirst[slot])*(t-1);
    }
    return total;
}
int main()
{
    int checks=0;
    for(unsigned slot=0;slot<CounterCount;++slot){
        if(slot==S) continue;
        for(int start : {0,1,5,11,12,100}){
            for(int delta : {1,2,4}){
                Counters before{};before[N]=200;before[slot]=start;
                auto after=before;after[slot]+=delta;
                const auto award=sceneAward(before,after,45000,0,{});
                assert(award && award->corruption==oracle(slot,start,delta)/11);
                ++checks;
            }
        }
    }
    for(int n : {0,1,11,12,13,50,51,100,101,199,200,201}){
        for(int progress : {0,1,10,11,12}){
            Counters before{};before[N]=n;
            auto after=before;after[S]=4;
            const auto award=sceneAward(before,after,45000,progress,{});
            std::int64_t base=0,extra=0;
            for(int j=1;j<=4;++j){
                const int t=std::min(progress+j,12);
                base+=n==0?3*t:soloCorruption(n);extra+=4*t;
            }
            assert(award->corruption==base+extra);
            assert(award->soloBaseK==base && award->soloExtraK==extra);
            assert(award->soloProgress==std::min(12,progress+4));++checks;
        }
    }
    Counters before{},after{};after[N]=after[M]=after[VG]=1;
    assert(sceneAward(before,after,0,0,{})->corruption==4);
    after={};after[S]=after[VG]=1;
    assert(sceneAward(before,after,0,0,{})->corruption==8);
    after={};after[N]=after[C]=after[AN]=after[V]=1;
    Investment nodes;nodes.enabled=true;
    for(Node node : {Node::Affinity,Node::Anal1,Node::Anal2,Node::Creature1,Node::Creature2,
                    Node::Victim1,Node::Victim2,Node::Erosion})
        assert(purchase(nodes,node,200));
    assert(sceneAward(before,after,0,0,nodes)->corruption==43);
    nodes.enabled=false;
    assert(sceneAward(before,after,0,0,nodes)->corruption==16);
    for(int tier=1;tier<8;++tier){
        assert(corruptionTier(corruptionThresholds[tier]-1)==tier-1);
        assert(corruptionTier(corruptionThresholds[tier])==tier);
        assert(corruptionTier(corruptionThresholds[tier]+1)==tier);
    }
    for(int tier=1;tier<5;++tier){
        assert(purityTier(purityThresholds[tier]-1)==tier-1);
        assert(purityTier(purityThresholds[tier])==tier);
    }
    assert(engineRates({},-100,5999).spellCostBasisPoints==1800);
    assert(engineRates({},-100,6000).spellCostBasisPoints==2000);
    assert(engineRates({},0,45000).incomingDamageBasisPoints==1500);
    assert(engineRates({},0,13500).spellCostBasisPoints==500);
    assert(destructionDamageBonus(1,45000).corruptionPercent==30);
    ProgressionControl qa;qa.enterDeveloperMode();qa.expire(1000);
    assert(qa.setMockSatisfaction(100));assert(qa.setMockCorruption(1499));
    Counters inc{};inc[N]=inc[M]=inc[VG]=1;
    assert(qa.mockScene(inc));assert(qa.current().corruption==1503);
    assert(qa.current().recentBlocks.front()->actualBlock==0);
    qa.advanceMockHours(8);assert(qa.current().satisfaction.total()==0);
    assert(qa.resetAll() && qa.current().soloProgress==0);
    assert(!qa.beginRealRead() && !qa.stateForSave());
    std::cout<<"PASS CR2 "<<checks<<" acquisition cases plus node, tier, saturation, expiry and reset checks"<<std::endl;

    const std::array<Counters,5> scenes{
        Counters{1,1,0,0,0,0,0,1,0,0},
        Counters{1,1,0,0,0,0,1,1,0,0},
        Counters{1,2,0,0,0,0,0,1,0,1},
        Counters{1,0,0,4,0,0,1,1,1,1},
        Counters{0,0,0,0,1,0,0,1,0,0}};
    std::ofstream sim("../SIMULATION.csv");
    sim<<"scenario,build,target,scenes,N,S,K,spent,purity_scenes_old,purity_scenes_candidate"<<std::endl;
    for(int scenario=0;scenario<6;++scenario) for(bool buy : {false,true}){
        Counters counts{};int progress=0;std::int64_t k=0;
        Investment owned;owned.enabled=buy;
        std::array<bool,4> reached{};
        int oldPurity=0,candidatePurity=0;
        for(int step=1;step<=100000;++step){
            const auto& delta=scenes[scenario==5?(step-1)%5:scenario];
            assert(validMockScene(delta));
            if(buy) for(Node node : {Node::Affinity,Node::Anal1,Node::Anal2,Node::Creature1,
                                     Node::Creature2,Node::Victim1,Node::Victim2,Node::Erosion})
                purchase(owned,node,counts[N]);
            auto next=counts;for(unsigned i=0;i<CounterCount;++i) next[i]+=delta[i];
            oldPurity+=k<900;candidatePurity+=k<6000;
            const auto award=sceneAward(counts,next,k,progress,owned);
            assert(award);k+=award->corruption;progress=award->soloProgress;counts=next;
            bool all=true;
            const std::array targets{30000,40000,45000,50000};
            for(unsigned i=0;i<targets.size();++i){
                if(!reached[i] && k>=targets[i]){
                    reached[i]=true;sim<<scenario<<','<<(buy?"progressive_43P":"base")<<','<<targets[i]<<','
                    <<step<<','<<counts[N]<<','<<counts[S]<<','<<k<<','<<owned.spent()<<','
                    <<oldPurity<<','<<candidatePurity<<std::endl;
                }
                all&=reached[i];
            }
            if(all)break;
            assert(step<100000);
        }
    }
}
