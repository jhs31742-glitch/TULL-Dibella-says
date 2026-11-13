#include "EquipmentLookup.h"
#include <array>
#include <cassert>
#include <iostream>
#include <type_traits>
enum class Kind {None,Weapon,Armor};
struct Bound {static constexpr Kind FORMTYPE=Kind::None;unsigned id;};
struct Weapon:Bound {static constexpr Kind FORMTYPE=Kind::Weapon;};
struct Armor:Bound {static constexpr Kind FORMTYPE=Kind::Armor;};
struct Provider {
 std::array<Weapon,2> weapons;std::array<Armor,2> armors;bool missing=false,wrongType=false;unsigned queries=0;
 template<class T>T* LookupForm(unsigned id,std::string_view plugin){
  ++queries;if(missing || plugin!="TULL Dibella says.esp" || id<0x800 || id>0x803)return nullptr;
  // Mirror the installed SDK's exact T::FORMTYPE filter, not an inheritance cast.
  auto kind=id<0x802?Kind::Weapon:Kind::Armor;if(wrongType)kind=Kind::None;
  if(kind!=T::FORMTYPE)return nullptr;
  if constexpr(std::is_same_v<T,Weapon>)return &weapons[id-0x800];
  if constexpr(std::is_same_v<T,Armor>)return &armors[id-0x802];
  return nullptr;
 }
};
int main(){Provider p;unsigned checks=0;
 for(unsigned g=0;g<4;++g){
  assert(p.LookupForm<Bound>(0x800+g,"TULL Dibella says.esp")==nullptr);++checks;
  auto* item=tds::lookupEquipmentSample<Bound,Weapon,Armor>(p,g,"TULL Dibella says.esp");
  Bound* expected=g<2?static_cast<Bound*>(&p.weapons[g]):static_cast<Bound*>(&p.armors[g-2]);assert(item==expected);++checks;
  p.missing=true;assert((tds::lookupEquipmentSample<Bound,Weapon,Armor>(p,g,"TULL Dibella says.esp")==nullptr));p.missing=false;++checks;
  p.wrongType=true;assert((tds::lookupEquipmentSample<Bound,Weapon,Armor>(p,g,"TULL Dibella says.esp")==nullptr));p.wrongType=false;++checks;
  assert((tds::lookupEquipmentSample<Bound,Weapon,Armor>(p,g,"Wrong.esp")==nullptr));++checks;
 }
 auto queries=p.queries;assert((tds::lookupEquipmentSample<Bound,Weapon,Armor>(p,4,"TULL Dibella says.esp")==nullptr));assert(p.queries==queries);checks+=2;
 std::cout<<"PASS "<<checks<<" lookup contract checks; 1.0.0 base-type rejection reproduced for all four samples; concrete lookups accepted\n";
}
