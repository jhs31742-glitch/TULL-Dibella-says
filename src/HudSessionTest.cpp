#include "HudSession.h"
#include <cassert>
#include <iostream>
int main(){tds::panel::HudSession s;assert(!s.enabled);s.enabled=true;s.layout="1,40,20,120,1";s.reset();assert(!s.enabled&&s.layout=="0,84,70,100,0");s.reset();assert(!s.enabled);std::cout<<"PASS HUD session boundary reset and default layout"<<std::endl;}
