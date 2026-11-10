#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace tds::progression {

inline constexpr std::array gxpThresholds{
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    14, 16, 18, 20, 22, 24, 27, 30, 33, 36, 39,
    43, 47, 51, 55, 59, 63, 68, 73, 78, 83, 88, 93,
    99, 105, 111, 117, 123, 130, 137, 144, 151, 158,
    166, 174, 182, 191, 200
};

inline constexpr std::array corruptionThresholds{0, 100, 250, 500, 900, 1400, 2000, 3000};

struct EffectStep {
    int threshold;
    int percent;
};

inline constexpr std::array positiveSteps{
    EffectStep{5, 6}, EffectStep{10, 12}, EffectStep{15, 19}, EffectStep{20, 26},
    EffectStep{25, 33}, EffectStep{30, 40}, EffectStep{40, 50}, EffectStep{50, 60},
    EffectStep{60, 70}, EffectStep{70, 78}, EffectStep{80, 85}, EffectStep{90, 93},
    EffectStep{100, 100}
};
inline constexpr std::array negativeSteps{
    EffectStep{10, 7}, EffectStep{20, 15}, EffectStep{30, 23}, EffectStep{40, 31},
    EffectStep{50, 40}, EffectStep{60, 50}, EffectStep{70, 60}, EffectStep{75, 67},
    EffectStep{80, 74}, EffectStep{85, 81}, EffectStep{90, 87}, EffectStep{95, 94},
    EffectStep{100, 100}
};

// Callers must first verify the provider snapshot. Negative counts are invalid.
[[nodiscard]] constexpr int earnedGxp(int sexCount)
{
    return static_cast<int>(std::upper_bound(gxpThresholds.begin(), gxpThresholds.end(), sexCount) -
                            gxpThresholds.begin());
}

[[nodiscard]] constexpr int corruptionTier(int corruption)
{
    return static_cast<int>(std::upper_bound(corruptionThresholds.begin(), corruptionThresholds.end(),
                                              std::max(0, corruption)) - corruptionThresholds.begin()) - 1;
}

[[nodiscard]] constexpr int appliedSatisfaction(std::int64_t blockTotal)
{
    return static_cast<int>(std::clamp<std::int64_t>(blockTotal, -100, 100));
}

// The sign selects the effect family, and the magnitude is its percentage.
[[nodiscard]] constexpr int satisfactionEffectPercent(int satisfaction)
{
    const auto applied = appliedSatisfaction(satisfaction);
    const auto steps = applied < 0 ? negativeSteps : positiveSteps;
    const auto magnitude = applied < 0 ? -applied : applied;
    const auto next = std::upper_bound(steps.begin(), steps.end(), magnitude,
        [](int value, EffectStep step) { return value < step.threshold; });
    const auto percent = next == steps.begin() ? 0 : (next - 1)->percent;
    return applied < 0 ? -percent : percent;
}

}  // namespace tds::progression