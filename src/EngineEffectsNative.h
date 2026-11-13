#pragma once

#include "EngineRates.h"
#include "OwnedCostPolicy.h"
#include "SpellDamageNative.h"

namespace tds::engine_effects {

inline RE::BGSPerk* costPerk{};
inline RE::BGSEntryPointFunctionDataOneValue* costValue{};
inline std::atomic_int incomingRate{};
using HealthDamage = void (*)(RE::Actor*, RE::Actor*, float);
inline HealthDamage nextDamage{};
using ModifyValue = void (*)(RE::ActorValueOwner*, RE::ACTOR_VALUE_MODIFIER, RE::ActorValue, float);
inline ModifyValue nextValue{};
inline thread_local bool insideDamage{};
inline void (*traceDamage)(float,float){};
inline std::atomic_uint tracedDamage{};

inline bool resolveCost(RE::TESDataHandler& data, const char* plugin)
{
    auto* perk=data.LookupForm<RE::BGSPerk>(0xFD0,plugin);
    if (!perk || perk->perkEntries.size()!=1) return false;
    auto* base=perk->perkEntries.front();
    if (!base || base->GetType()!=RE::PERK_ENTRY_TYPE::kEntryPoint) return false;
    auto* entry=static_cast<RE::BGSEntryPointPerkEntry*>(base);
    if (entry->entryData.entryPoint.underlying()!=38 ||
        entry->entryData.function.underlying()!=3 || entry->entryData.numArgs!=2 ||
        !entry->functionData ||
        entry->functionData->GetType()!=RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) return false;
    auto* value=static_cast<RE::BGSEntryPointFunctionDataOneValue*>(entry->functionData);
    if (value->data!=1.0f) return false;
    costValue=value;
    costPerk=perk;
    return true;
}

inline bool reconcileCost(RE::PlayerCharacter& player, int basisPoints)
{
    if (!costPerk || !costValue) return false;
    return progression::synchronizeOwnedCost(costValue->data,basisPoints,
        [&]{return player.HasPerk(costPerk);},[&]{player.AddPerk(costPerk);});
}

template <class Original>
void scaleDamage(float value, Original original)
{
    const auto rate=incomingRate.load(std::memory_order_relaxed);
    if (insideDamage || !rate || !std::isfinite(value) || value>=0.0f) {
        original(value);
        return;
    }
    const float adjusted=value*(1.0f+static_cast<float>(rate)/10000.0f);
    struct Reset { ~Reset(){ insideDamage=false; } };
    insideDamage=true;
    const Reset reset;
    if (traceDamage && tracedDamage.fetch_add(1,std::memory_order_relaxed)<120) traceDamage(value,adjusted);
    original(std::isfinite(adjusted) ? adjusted : value);
}

inline void handleHealthDamage(RE::Actor* target, RE::Actor* source, float value)
{
    if (!target || target!=RE::PlayerCharacter::GetSingleton()) {
        nextDamage(target,source,value);
        return;
    }
    scaleDamage(value,[&](float adjusted){nextDamage(target,source,adjusted);});
}

inline void modifyHealthValue(RE::ActorValueOwner* owner, RE::ACTOR_VALUE_MODIFIER modifier,
                              RE::ActorValue av, float value)
{
    auto* player=RE::PlayerCharacter::GetSingleton();
    if (!player || owner!=player->AsActorValueOwner() ||
        modifier!=RE::ACTOR_VALUE_MODIFIER::kDamage || av!=RE::ActorValue::kHealth) {
        nextValue(owner,modifier,av,value);
        return;
    }
    scaleDamage(value,[&](float adjusted){nextValue(owner,modifier,av,adjusted);});
}

inline bool installDamage()
{
    if (nextDamage) return true;
    if (REL::Module::get().version()!=SKSE::RUNTIME_SSE_1_5_97) return false;
    REL::Relocation<std::uintptr_t> damageTable{RE::PlayerCharacter::VTABLE[0]};
    REL::Relocation<std::uintptr_t> valueTable{RE::PlayerCharacter::VTABLE[5]};
    constexpr std::size_t damageSlot=0x104, valueSlot=0x06;
    const auto damage=reinterpret_cast<const std::uintptr_t*>(damageTable.address())[damageSlot];
    const auto value=reinterpret_cast<const std::uintptr_t*>(valueTable.address())[valueSlot];
    if (!spell_native::allowedPredecessor(damage) || !spell_native::allowedPredecessor(value)) return false;
    nextDamage=reinterpret_cast<HealthDamage>(damage);
    nextValue=reinterpret_cast<ModifyValue>(value);
    damageTable.write_vfunc(damageSlot,handleHealthDamage);
    valueTable.write_vfunc(valueSlot,modifyHealthValue);
    return true;
}

}  // namespace tds::engine_effects
