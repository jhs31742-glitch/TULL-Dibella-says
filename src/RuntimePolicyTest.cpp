#include "RuntimePolicy.h"
#include <cstdlib>
#include <iostream>

int main() {
    using namespace tds::runtime;
    unsigned checks=0;
    const auto require=[&](bool value){++checks;if(!value)std::exit(1);};
    for (auto version:supported) {
        require(accepts(version));
        const auto site=hitSite(version);
        require(site.ae==(version.minor==6));
        require(site.id==(version.minor==6?38586:37633));
        require(site.offset==(version.minor==6?0xFA:0x16A));
    }
    for (auto version:{Version{1,4,15,0},Version{1,5,96,0},Version{1,6,1170,1},Version{1,6,9999,0},Version{1,7,0,0},Version{0,6,1170,0}}) {
        require(!accepts(version));require(hitSite(version).id==0);
    }
    require(healthDamageSlot==0x104);require(modifyValueSlot==0x06);require(spellValueSlot==0x20);
    std::cout<<"PASS "<<checks<<" runtime admission/call-site/ABI-family boundary checks\n";
}
