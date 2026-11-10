#pragma once

#include "Phase1.h"

namespace RE {
class EnchantmentItem;
class ExtraDataList;
}

namespace tds {

using EnchantmentRecords = std::array<const RE::EnchantmentItem*, 4>;

// The caller must select the actual worn instance and derive its category
// from its base form. A missing worn list may be used for a built-in ENCH
// only if there is no ambiguity about which instance is equipped.
[[nodiscard]] std::optional<Group> classifyEnchantment(
    const RE::ExtraDataList* worn, const RE::EnchantmentItem* builtIn,
    bool weapon, const EnchantmentRecords& records);

}  // namespace tds
