#include "EquipmentClaims.h"
#include <cassert>
#include <iostream>
int main(){
 unsigned checks=0;
 for(std::uint32_t mask=0;mask<16;++mask){
  assert(tds::decodeEquipmentClaim(1,4,4,mask,false)==mask);++checks;
  for(unsigned g=0;g<4;++g){auto live=mask;const auto expected=!(mask&(1u<<g));assert(tds::reserveEquipmentClaim(live,g)==expected);assert(live==(mask|(1u<<g)));assert(!tds::reserveEquipmentClaim(live,g));checks+=3;}
  auto live=mask;assert(!tds::reserveEquipmentClaim(live,4) && live==mask);assert(!tds::reserveEquipmentClaim(live,99));checks+=2;
 }
 for(unsigned v:{0u,2u,999u}){assert(tds::decodeEquipmentClaim(v,4,4,0,false)==15);++checks;}
 for(unsigned n:{0u,1u,3u,5u,999u}){assert(tds::decodeEquipmentClaim(1,n,4,0,false)==15);++checks;}
 assert(tds::decodeEquipmentClaim(1,4,3,0,false)==15);assert(tds::decodeEquipmentClaim(1,4,4,16,false)==15);assert(tds::decodeEquipmentClaim(1,4,4,0,true)==15);checks+=3;
 std::uint32_t live=1;auto mock=live;assert(tds::reserveEquipmentClaim(mock,2));assert(live==1 && mock==5);checks+=2;
 // Existing saves without the optional claim record begin unclaimed, independently of growth reset.
 std::uint32_t olderSave=0;assert(tds::reserveEquipmentClaim(olderSave,3));assert(olderSave==8);checks+=2;
 std::cout<<"PASS "<<checks<<" equipment claim, duplicate, invalid record and mock isolation checks\n";
}
