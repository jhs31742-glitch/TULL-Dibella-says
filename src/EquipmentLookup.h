#pragma once
#include <string_view>
namespace tds {
// TESDataHandler::LookupForm<T> requires exact T::FORMTYPE. Do not query an inherited base type.
template<class BoundObject,class Weapon,class Armor,class Handler>
BoundObject* lookupEquipmentSample(Handler& data,unsigned group,std::string_view plugin) {
 if(group>=4) return nullptr;
 if(group<2) return data.template LookupForm<Weapon>(0x800+group,plugin);
 return data.template LookupForm<Armor>(0x800+group,plugin);
}
}
