#pragma once

#include "Phase1.h"

namespace tds {

// This contract is limited to the two audited loose sslActorStats.pex files.
// Verifying the effective file and VM readiness belongs to the game adapter.
inline constexpr std::string_view frameworkStatsSha =
    "82270f9fcaf8618b83c5e56956890a927aadc12f1d3c4d2c1ed5bbacfb29ca9f";
inline constexpr std::string_view utilityPlusStatsSha =
    "c63635b640d0ea48a794445cc3acf1451fd0b4435745344d550ab7953636463f";

// sslActorStats property getters in statNames order, verified for both hashes.
inline constexpr std::array skillIndices{12, 6, 7, 8, 9, 10, 11, 18, 19, 20, 13};
using NativeValues = std::array<std::optional<float>, statNames.size()>;

[[nodiscard]] Snapshot decodeNativeStats(std::string_view verifiedSha, ReadStatus status,
                                         const NativeValues& values);

}  // namespace tds
