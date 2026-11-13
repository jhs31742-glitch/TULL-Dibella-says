#include "OwnedCostPolicy.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
float value=1;bool present=false;int adds=0;
auto has=[&]{return present;};auto add=[&]{present=true;++adds;};
for(int bp:{0,-1500,500,0,2000,-1500,0}){
assert(tds::progression::synchronizeOwnedCost(value,bp,has,add));
assert(std::abs(value-(1.0f+bp/10000.0f))<0.00001f);}
assert(adds==1&&present&&value==1);
value=.85f;assert(!tds::progression::synchronizeOwnedCost(value,500,[]{return false;},[]{}));assert(value==.85f);
assert(!tds::progression::synchronizeOwnedCost(value,-10000,has,add));assert(value==.85f);
std::cout<<"PASS owned cost in-place transitions, neutralization, registration failure rollback, invalid factor"<<std::endl;
}
