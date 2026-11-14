#include "SaveSettings.h"
#include "HudSession.h"
#include <cstring>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace tds;
    unsigned checks=0;
    const auto require=[&](bool value){++checks;if(!value){std::cerr<<"FAILED check "<<checks<<'\n';std::exit(1);}};
    SaveSettings a{33+512,1,1,23,41,125,1,1},b{34+256,0,0,84,70,100,0,0};
    require(validSettings(a));require(validSettings(b));
    auto disk=settingsForSave(false,a);require(disk==a);
    SaveSettings bytes{};
    std::memcpy(bytes.data(),disk->data(),sizeof(bytes));
    auto decoded=decodeSettings(1,sizeof(bytes),sizeof(bytes),bytes,false);
    require(decoded==a);
    require(settingsForLoad(decoded,true,b)==a); // same save / restarted process
    auto other=settingsForSave(false,b);
    require(settingsForLoad(other,true,b)==b); // A -> B
    require(settingsForLoad(decoded,true,b)==a); // B -> A
    require(settingsForLoad(std::nullopt,true,b)==b); // old/no co-save, no stale A
    require(settingsForLoad(decoded,false,b)==b); // NewGame discards previous save
    auto qa=a;qa[0]=25;qa[3]=99;qa[7]=0;
    require(!settingsForSave(true,qa));
    require(settingsForLoad(decoded,true,b)==a); // QA changes never overwrite A
    for (unsigned version:{0u,2u,100u})require(!decodeSettings(version,sizeof(bytes),sizeof(bytes),bytes,false));
    for (unsigned length:{0u,1u,31u,33u,100u})require(!decodeSettings(1,length,sizeof(bytes),bytes,false));
    require(!decodeSettings(1,sizeof(bytes),sizeof(bytes)-1,bytes,false));
    require(!decodeSettings(1,sizeof(bytes),sizeof(bytes),bytes,true)); // duplicate
    for(unsigned i=0;i<a.size();++i){auto bad=a;bad[i]=0xffffffffu;require(!validSettings(bad));}
    for(unsigned key:{0u,42u,54u,29u,157u,56u,184u,2048u}){
        auto bad=a;bad[0]=key;require(!validSettings(bad));
    }
    for(unsigned x:{0u,100u})for(unsigned y:{0u,100u})for(unsigned size:{50u,160u})
        for(unsigned dir:{0u,1u})for(unsigned style:{0u,1u})for(unsigned on:{0u,1u}){
            auto value=a;value[1]=on;value[2]=dir;value[3]=x;value[4]=y;value[5]=size;value[6]=style;
            require(validSettings(value));auto parsed=b;
            require(captureHudLayout(hudLayout(value),parsed));
            for(unsigned i=2;i<=6;++i)require(parsed[i]==value[i]);
            panel::HudSession session;session.enabled=true;session.layout=hudLayout(value);
            session.reset();require(!session.enabled && session.layout==hudLayout(defaultSettings));
            session.enabled=value[1]!=0;session.layout=hudLayout(value);
            require(session.enabled==(on!=0) && session.layout==hudLayout(value));
        }
    for(auto text:{"","1,2,3,100","1,2,3,100,0,8","2,2,3,100,0","0,101,3,100,0","0,2,3,49,0","0,2,3,161,0","0,2,-3,100,0","x,2,3,100,0"}) {
        auto original=a;require(!captureHudLayout(text,original));require(original==a);
    }
    require(!settingsForSave(false,SaveSettings{}));
    std::cout<<"PASS "<<checks<<" save settings roundtrip, A/B isolation, missing/invalid records, HUD bounds, QA exclusion checks\n";
}
