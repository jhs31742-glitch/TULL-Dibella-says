#include "Phase1.h"
#include "SexLabStats.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
int checks{};
void require(bool condition, const char* name)
{
    ++checks;
    if (!condition) {
        throw std::runtime_error(name);
    }
}

double cumulative(int stage, int early, int late)
{
    constexpr std::array<int, 13> partial{0, 8, 14, 19, 23, 26, 28, 30, 31, 32, 33, 34, 35};
    if (stage <= 12) {
        return early * partial[stage] / 35.0;
    }
    if (stage <= 28) {
        return early + 30.0 * (stage - 12) / 16.0;
    }
    return 100.0 - late * partial[40 - stage] / 35.0;
}

int rounded(double amount)
{
    return static_cast<int>(std::floor(amount + 0.5));
}

void growth()
{
    using namespace tds;
    constexpr std::array official{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 21,
        24, 27, 31, 35, 39, 44, 49, 56, 63, 73, 83, 97, 111, 130,
        149, 174, 199, 231, 263, 303, 343, 392, 441, 500
    };
    require(thresholds == official, "official thresholds");
    for (std::size_t i = 0; i < official.size(); ++i) {
        const int stage = static_cast<int>(i) + 1;
        require(growthStage(official[i] - 1) == stage - 1, "threshold minus one");
        require(growthStage(official[i]) == stage, "threshold entry");
    }

    constexpr std::array steal{12, 22, 28, 34, 40};
    constexpr std::array absorb{1, 3, 6, 12, 19, 22, 29, 35, 38, 40};
    for (int stage = 0; stage <= 40; ++stage) {
        const int count = stage ? official[stage - 1] : 0;
        const int mirrored = stage < 40 ? official[39 - stage] : 0;
        const auto w = calculate(Group::WeaponGrowth, count);
        const auto a = calculate(Group::AccessoryGrowth, count);
        require(w.stage == stage && a.stage == stage, "stage");
        require(calculate(Group::WeaponDecay, count) == calculate(Group::WeaponGrowth, mirrored),
                "weapon mirror");
        require(calculate(Group::AccessoryDecay, count) == calculate(Group::AccessoryGrowth, mirrored),
                "accessory mirror");

        const auto ordinary = [stage](int max) {
            if (stage <= 20) {
                return rounded(max * cumulative(stage, 35, 35) / 100.0);
            }
            return max - rounded(max * cumulative(40 - stage, 35, 35) / 100.0);
        };
        require(w.weapon == Weapon{rounded(30 * cumulative(stage, 45, 25) / 100.0),
                                   rounded(20 * cumulative(stage, 25, 45) / 100.0),
                                   ordinary(100), ordinary(100),
                                   static_cast<int>(std::ranges::upper_bound(steal, stage) - steal.begin())},
                "all weapon fields");
        require(a.accessory == Accessory{ordinary(30), ordinary(100), ordinary(100),
                                         ordinary(50), ordinary(50), ordinary(10),
                                         ordinary(30), ordinary(30), ordinary(30),
                                         static_cast<int>(std::ranges::upper_bound(absorb, stage) - absorb.begin())},
                "all accessory fields");
    }
    require(calculate(Group::AccessoryGrowth, 392).accessory.spellAbsorption == 9, "392 milestone");
    for (auto group : {Group::WeaponGrowth, Group::WeaponDecay, Group::AccessoryGrowth, Group::AccessoryDecay}) {
        for (int count : {501, 1000, 1001, std::numeric_limits<int>::max()}) {
            require(calculate(group, count) == calculate(group, 500), "cap");
        }
    }
    require(calculate(Group::WeaponGrowth, 500).weapon == Weapon{30, 20, 100, 100, 5}, "weapon maximum");
    require(calculate(Group::AccessoryGrowth, 500).accessory ==
            Accessory{30, 100, 100, 50, 50, 10, 30, 30, 30, 10}, "accessory maximum");
    require(calculate(Group::WeaponDecay, 500).weapon == Weapon{}, "weapon zero");
    require(calculate(Group::AccessoryDecay, 500).accessory == Accessory{}, "accessory zero");

    constexpr std::array equipped{Group::WeaponGrowth, Group::WeaponGrowth, Group::WeaponDecay,
                                  Group::AccessoryGrowth, Group::AccessoryDecay};
    const auto both = targets(equipped, 392);
    require(std::ranges::all_of(both, [](const auto& target) { return target.has_value(); }), "four groups");
    require(targets(std::span(equipped).subspan(1), 392) == both, "same group remains");
    require(!targets(std::span(equipped).subspan(2), 392)[0], "last owner removed");
    require(targets(equipped, std::nullopt) == GroupTargets{}, "no targets without count");
}

void logging()
{
    using namespace tds;
    std::ostringstream out;
    StatLogger logger(out);
    constexpr std::array equipped{Group::WeaponGrowth, Group::WeaponGrowth};
    const auto session = logger.beginSession();
    NativeValues native{12.0f, 7.0f, 6.0f, 1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f};
    auto baseline = decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native);
    require(baseline.status == ReadStatus::Valid, "valid read");
    require(decodeNativeStats(frameworkStatsSha, ReadStatus::Valid, native).values == baseline.values, "framework");
    require(decodeNativeStats("unknown", ReadStatus::Valid, native).status == ReadStatus::NotReady, "unsupported");
    logger.observe(session, Event::Load, baseline, equipped);
    require(out.str().find("d_SexCount=") == std::string::npos, "load has no delta");

    for (const auto status : {ReadStatus::NotReady, ReadStatus::Error, ReadStatus::Stale}) {
        logger.observe(session, Event::SceneEnd, decodeNativeStats(utilityPlusStatsSha, status, native), equipped);
        require(logger.baseline() == baseline.values, "failed or stale sample leaves baseline");
    }
    native[9].reset();
    require(decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native).status == ReadStatus::Error, "missing");
    native[9] = std::numeric_limits<float>::infinity();
    require(decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native).status == ReadStatus::Error, "infinite");
    native[9] = std::numeric_limits<float>::quiet_NaN();
    require(decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native).status == ReadStatus::Error, "nan");
    native[9] = 2147483648.0f;
    require(decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native).status == ReadStatus::Error, "overflow");
    native[9] = -1.0f;
    require(decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native).status == ReadStatus::Error, "negative");
    for (std::size_t i = 0; i < native.size(); ++i) {
        native[i] = static_cast<float>(baseline.values[i] + i + 1);
    }
    auto increased = decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native);
    logger.observe(session, Event::SceneEnd, increased, equipped);
    for (std::size_t i = 0; i < statNames.size(); ++i) {
        require(out.str().find(" d_" + std::string(statNames[i]) + '=' + std::to_string(i + 1)) != std::string::npos,
                "each stat delta");
    }
    logger.observe(session, Event::SceneEnd, increased, equipped);
    require(out.str().find("d_SexCount=0") != std::string::npos, "duplicate delta zero");
    native.fill(0.0f);
    logger.observe(session, Event::Refresh, decodeNativeStats(utilityPlusStatsSha, ReadStatus::Valid, native), equipped);
    require(out.str().find("decreased=1 reset=UNCONFIRMED") != std::string::npos, "decrease not classified as reset");
    require(logger.baseline() == Stats{}, "valid zero");
    const auto newSession = logger.beginSession();
    logger.observe(session, Event::SceneEnd, increased, equipped);
    require(!logger.baseline(), "old session ignored");
    logger.observe(newSession, Event::Load, baseline, equipped);
    require(logger.baseline() == baseline.values, "fresh load baseline");
}
}  // namespace

int main()
{
    try {
        growth();
        logging();
        std::cout << "PASS " << checks << " checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
