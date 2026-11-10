#pragma once

#include "ProgressionRules.h"

#include <numeric>
#include <optional>

namespace tds::progression {

enum class Node : unsigned {
    Recovery1, Recovery2, Recovery3, Anal1, Anal2, Creature1, Creature2,
    Victim1, Victim2, CorruptionResist, SatisfactionResist, LongEcho, Passion,
    Solitude, Affinity, Dominance, Battle, Guard, Ward, Erosion, Predator, Carapace,
    MaleResist, FemaleResist, CreatureResist, Count
};

inline constexpr std::array nodeCosts{
    1, 2, 3, 2, 5, 2, 7, 3, 8, 8, 7, 4, 4, 5, 5, 5, 8, 7, 8, 11, 9, 9, 12, 12, 10
};
static_assert(nodeCosts.size() == static_cast<unsigned>(Node::Count));
static_assert(std::accumulate(nodeCosts.begin(), nodeCosts.end(), 0) == 157);

[[nodiscard]] constexpr unsigned index(Node node) { return static_cast<unsigned>(node); }

struct Investment {
    std::array<bool, nodeCosts.size()> purchased{};
    bool enabled{true};

    [[nodiscard]] constexpr bool owns(Node node) const
    {
        return index(node)<purchased.size() && purchased[index(node)];
    }
    [[nodiscard]] constexpr bool active(Node node) const { return enabled && owns(node); }
    [[nodiscard]] constexpr int spent() const
    {
        int total = 0;
        for (unsigned i = 0; i < purchased.size(); ++i) {
            if (purchased[i]) total += nodeCosts[i];
        }
        return total;
    }
};

inline constexpr std::array prerequisites{
    std::pair{Node::Recovery2,Node::Recovery1}, std::pair{Node::Recovery3,Node::Recovery2},
    std::pair{Node::Anal2,Node::Anal1}, std::pair{Node::Creature2,Node::Creature1},
    std::pair{Node::Victim2,Node::Victim1}
};
inline constexpr std::array exclusions{
    std::pair{Node::CorruptionResist,Node::Erosion}, std::pair{Node::LongEcho,Node::Passion},
    std::pair{Node::Solitude,Node::Affinity}, std::pair{Node::MaleResist,Node::FemaleResist}
};

enum class PurchaseStatus { Available, Owned, Disabled, Unverified, Prerequisite, Exclusive, Insufficient, Invalid };

[[nodiscard]] constexpr PurchaseStatus purchaseStatus(const Investment& state, Node node, int verifiedCount)
{
    if (index(node)>=nodeCosts.size()) return PurchaseStatus::Invalid;
    if (state.owns(node)) return PurchaseStatus::Owned;
    if (!state.enabled) return PurchaseStatus::Disabled;
    if (verifiedCount<0) return PurchaseStatus::Unverified;
    for (const auto [dependent,required] : prerequisites) {
        if (node==dependent && !state.owns(required)) return PurchaseStatus::Prerequisite;
    }
    for (const auto [left,right] : exclusions) {
        if ((node==left && state.owns(right)) || (node==right && state.owns(left))) return PurchaseStatus::Exclusive;
    }
    if (state.spent()+nodeCosts[index(node)]>earnedGxp(verifiedCount)) return PurchaseStatus::Insufficient;
    return PurchaseStatus::Available;
}

[[nodiscard]] constexpr bool canPurchase(const Investment& state, Node node, int verifiedCount)
{
    return purchaseStatus(state,node,verifiedCount)==PurchaseStatus::Available;
}

constexpr bool purchase(Investment& state, Node node, int verifiedCount)
{
    if (!canPurchase(state,node,verifiedCount)) return false;
    state.purchased[index(node)]=true;
    return true;
}

[[nodiscard]] constexpr std::optional<std::size_t> karmaAbilityIndex(const Investment& nodes, std::int64_t karma)
{
    const auto tier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(karma,0,3000)));
    if (tier==0) return std::nullopt;
    const auto variant=nodes.active(Node::CorruptionResist) ? 1 : nodes.active(Node::Erosion) ? 2 : 0;
    constexpr std::size_t legacyEnd=5+3*(corruptionThresholds.size()-1)+positiveSteps.size()+2*negativeSteps.size();
    if (variant==0) return static_cast<std::size_t>(5+(tier-1)*3);
    return legacyEnd+static_cast<std::size_t>((tier-1)*2+variant-1);
}

[[nodiscard]] constexpr std::optional<std::size_t> satisfactionAbilityIndex(const Investment& nodes, std::int64_t x, std::int64_t karma)
{
    const auto percent=satisfactionEffectPercent(appliedSatisfaction(x));
    if (percent==0) return std::nullopt;
    const auto& steps=percent<0 ? negativeSteps : positiveSteps;
    const int magnitude=percent<0 ? -percent : percent;
    const auto step=std::find_if(steps.begin(),steps.end(),
        [magnitude](EffectStep candidate){return candidate.percent==magnitude;})-steps.begin();
    constexpr auto first=5+3*(corruptionThresholds.size()-1);
    if (percent>0) return first+step;
    const auto index=static_cast<std::size_t>(step)*2+(nodes.active(Node::SatisfactionResist)?1:0);
    const auto karmaTier=corruptionTier(static_cast<int>(std::clamp<std::int64_t>(karma,0,3000)));
    if (karmaTier>=4) return first+positiveSteps.size()+index;
    constexpr auto lowKarmaFirst=first+positiveSteps.size()+2*negativeSteps.size()+2*(corruptionThresholds.size()-1);
    return lowKarmaFirst+static_cast<std::size_t>(karmaTier)*2*negativeSteps.size()+index;
}

struct PassiveEffects {
    int flatWeaponDamage{};
    int armor{};
    int elementalDiseasePoisonResist{};
    int magicResist{};
    int healthStealOnValidHit{};
    int baseResourceReductionPercent{};
};

[[nodiscard]] constexpr PassiveEffects passiveEffects(const Investment& state, int verifiedCount)
{
    PassiveEffects effects{};
    if (state.active(Node::Battle)) effects.flatWeaponDamage+=6;
    if (state.active(Node::Guard)) effects.armor+=40;
    if (state.active(Node::Ward)) {
        effects.elementalDiseasePoisonResist=10;
        effects.magicResist=5;
    }
    if (state.active(Node::Predator)) {
        effects.flatWeaponDamage+=8;
        effects.armor-=40;
        effects.healthStealOnValidHit=3;
    }
    if (state.active(Node::Carapace)) {
        effects.armor+=80;
        effects.flatWeaponDamage-=6;
    }
    if (verifiedCount>0) {
        effects.baseResourceReductionPercent=90-30*(state.active(Node::Recovery1)+
            state.active(Node::Recovery2)+state.active(Node::Recovery3));
    }
    return effects;
}

enum class AttackerSex { Unknown, Male, Female };

// Classification is supplied by the game adapter after it has verified the attacker.
[[nodiscard]] constexpr int targetResistancePercent(const Investment& state, bool creature, AttackerSex sex)
{
    if (creature) return state.active(Node::CreatureResist)?50:0;
    if (sex==AttackerSex::Male && state.active(Node::MaleResist)) return 50;
    if (sex==AttackerSex::Female && state.active(Node::FemaleResist)) return 50;
    return 0;
}

}  // namespace tds::progression