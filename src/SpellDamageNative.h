#pragma once

#include "SpellDamagePolicy.h"
#include "NodeRules.h"
#include "RuntimePolicy.h"

namespace tds::spell_native {

inline std::atomic_uint bonus{};
inline thread_local bool inChain{};
inline void (*traceSpell)(float,float){};
inline std::atomic_uint tracedSpell{};
inline std::array<std::uintptr_t,4> next{};

inline void setBonus(progression::DestructionDamageBonus value)
{
    bonus.store(static_cast<unsigned>(value.purityPercent+value.corruptionPercent),std::memory_order_relaxed);
}

inline std::atomic_uint resistanceMask{};

inline void setResistance(const progression::Investment& state)
{
    using Node=progression::Node;
    resistanceMask.store((state.active(Node::CreatureResist)?4u:0u) |
                         (state.active(Node::FemaleResist)?2u:0u) |
                         (state.active(Node::MaleResist)?1u:0u), std::memory_order_relaxed);
}

inline bool resists(RE::Actor* attacker)
{
    const auto mask=resistanceMask.load(std::memory_order_relaxed);
    const auto* race=mask && attacker ? attacker->GetRace() : nullptr;
    if (!race) return false;
    if (race->HasKeywordString("ActorTypeCreature")) return (mask & 4u)!=0;
    const auto* npc=attacker->GetActorBase();
    if (!npc || !race->HasKeywordString("ActorTypeNPC")) return false;
    switch (npc->GetSex()) {
    case RE::SEX::kMale: return (mask & 1u)!=0;
    case RE::SEX::kFemale: return (mask & 2u)!=0;
    default: return false;
    }
}

inline float scaledValue(RE::ActiveEffect* effect, RE::Actor* target, float value, RE::ActorValue av)
{
    const auto percent=bonus.load(std::memory_order_relaxed);
    if (!effect || !target || !std::isfinite(value) || value>=0.0f) return value;
    auto* player=RE::PlayerCharacter::GetSingleton();
    const auto* instance=effect->effect;
    const auto* base=instance ? instance->baseEffect : nullptr;
    const auto* spell=effect->spell ? effect->spell->As<RE::SpellItem>() : nullptr;
    using AV=RE::ActorValue;
    using Flag=RE::EffectSetting::EffectSettingData::Flag;
    if (!player || !spell || !base ||
        spell->GetSpellType()!=RE::MagicSystem::SpellType::kSpell ||
        spell->GetCastingType()!=RE::MagicSystem::CastingType::kFireAndForget ||
        (av!=AV::kHealth && av!=AV::kNone) || base->data.primaryAV!=AV::kHealth ||
        !base->IsHostile() || !base->IsDetrimental() ||
        base->data.flags.any(Flag::kRecover) || instance->GetDuration()!=0) return value;
    const auto caster=effect->GetCasterActor();
    if (target==player) {
        return caster && caster.get()!=player && resists(caster.get()) ? value*0.5f : value;
    }
    if (!percent || caster.get()!=player || spell->GetAssociatedSkill()!=AV::kDestruction) return value;
    const auto adjusted=progression::destructionDamage({true,true,true,true,true,false},-value,
        {static_cast<int>(percent),0});
    return adjusted ? -*adjusted : value;
}

template <std::size_t Slot, class Effect>
void thunk(Effect* effect,RE::Actor* target,float value,RE::ActorValue av)
{
    using Modify=void (*)(Effect*,RE::Actor*,float,RE::ActorValue);
    const auto original=reinterpret_cast<Modify>(next[Slot]);
    if (inChain) {
        original(effect,target,value,av);
        return;
    }
    struct Reset { ~Reset(){ inChain=false; } };
    inChain=true;
    const Reset reset;
    const float adjusted=scaledValue(effect,target,value,av);
    if (traceSpell && adjusted!=value && tracedSpell.fetch_add(1,std::memory_order_relaxed)<120) traceSpell(value,adjusted);
    original(effect,target,adjusted,av);
}

inline bool allowedPredecessor(std::uintptr_t address)
{
    if (!address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) ||
        info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD access=info.Protect&0xff;
    if (access!=PAGE_EXECUTE && access!=PAGE_EXECUTE_READ && access!=PAGE_EXECUTE_READWRITE &&
        access!=PAGE_EXECUTE_WRITECOPY) return false;
    const auto owner=reinterpret_cast<HMODULE>(info.AllocationBase);
    return owner==GetModuleHandleW(nullptr) || owner==GetModuleHandleW(L"pathoftullbung.dll");
}

inline bool install()
{
    const auto version=REL::Module::get().version();
    if (!runtime::accepts({version[0],version[1],version[2],version[3]})) return false;
    if (next.front()) return true;
    constexpr auto index=runtime::spellValueSlot;
    std::array tables{
        REL::Relocation<std::uintptr_t>{RE::VTABLE_ValueModifierEffect[0]},
        REL::Relocation<std::uintptr_t>{RE::VTABLE_DualValueModifierEffect[0]},
        REL::Relocation<std::uintptr_t>{RE::VTABLE_ValueAndConditionsEffect[0]},
        REL::Relocation<std::uintptr_t>{RE::VTABLE_PeakValueModifierEffect[0]}
    };
    const std::array replacements{
        reinterpret_cast<std::uintptr_t>(&thunk<0,RE::ValueModifierEffect>),
        reinterpret_cast<std::uintptr_t>(&thunk<1,RE::DualValueModifierEffect>),
        reinterpret_cast<std::uintptr_t>(&thunk<2,RE::ValueAndConditionsEffect>),
        reinterpret_cast<std::uintptr_t>(&thunk<3,RE::PeakValueModifierEffect>)
    };
    std::array<std::uintptr_t,4> prior{};
    for (unsigned i=0;i<tables.size();++i) {
        prior[i]=reinterpret_cast<const std::uintptr_t*>(tables[i].address())[index];
        if (!allowedPredecessor(prior[i])) return false;
    }
    for (unsigned i=0;i<tables.size();++i) next[i]=prior[i];
    for (unsigned i=0;i<tables.size();++i) {
        tables[i].write_vfunc(index,replacements[i]);
    }
    return true;
}

}  // namespace tds::spell_native
