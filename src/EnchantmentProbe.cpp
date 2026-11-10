#include <RE/Skyrim.h>

#include "EnchantmentProbe.h"

namespace tds {
namespace {

const RE::EffectSetting* singleMarker(const RE::EnchantmentItem& enchantment)
{
    if (enchantment.effects.size() != 1 || !enchantment.effects[0]) {
        return nullptr;
    }
    return enchantment.effects[0]->baseEffect;
}

}  // namespace

std::optional<Group> classifyEnchantment(const RE::ExtraDataList* worn,
    const RE::EnchantmentItem* builtIn, bool weapon, const EnchantmentRecords& records)
{
    const auto* extra = worn ? worn->GetByType<RE::ExtraEnchantment>() : nullptr;
    const auto* enchantment = extra ? extra->enchantment : builtIn;
    if (!enchantment) {
        return std::nullopt;
    }

    const auto* marker = singleMarker(*enchantment);
    if (!marker) {
        return std::nullopt;
    }

    std::optional<Group> result;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto* expected = records[i];
        if (!expected || marker != singleMarker(*expected)) {
            continue;
        }
        if (result || weapon != (i < 2) ||
            (enchantment != expected && enchantment->data.baseEnchantment != expected)) {
            return std::nullopt;
        }
        result = static_cast<Group>(i);
    }
    return result;
}

}  // namespace tds
