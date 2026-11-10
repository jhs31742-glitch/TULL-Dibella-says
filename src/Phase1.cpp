#include "Phase1.h"

#include <algorithm>
#include <ostream>
#include <numeric>

namespace tds {
namespace {

constexpr std::array weights{8, 6, 5, 4, 3, 2, 2, 1, 1, 1, 1, 1};
constexpr std::array stealSteps{12, 22, 28, 34, 40};
constexpr std::array absorbSteps{1, 3, 6, 12, 19, 22, 29, 35, 38, 40};
constexpr std::array groupNames{"WeaponGrowth", "WeaponDecay", "AccessoryGrowth", "AccessoryDecay"};
constexpr std::array statuses{"VALID", "NOT_READY", "ERROR", "STALE"};
constexpr std::array events{"LOAD", "NEW_GAME", "SCENE_END", "MANUAL", "UI_OPEN", "RECHECK"};
constexpr int denominator = 100 * 35 * 16;

int weighted(int stage, int maximum, int early, int late)
{
    const int earlySteps = std::min(stage, 12);
    const int middleSteps = std::clamp(stage - 12, 0, 16);
    const int lateSteps = std::max(stage - 28, 0);
    const int earlyWeight = std::accumulate(weights.begin(), weights.begin() + earlySteps, 0);
    const int lateWeight = std::accumulate(weights.end() - lateSteps, weights.end(), 0);
    const int numerator = early * earlyWeight * 16 + 30 * middleSteps * 35
        + late * lateWeight * 16;
    return (maximum * numerator + denominator / 2) / denominator;
}

int general(int stage, int maximum)
{
    if (stage <= 20) {
        return weighted(stage, maximum, 35, 35);
    }
    return maximum - weighted(40 - stage, maximum, 35, 35);
}

int milestones(std::span<const int> steps, int stage)
{
    return static_cast<int>(std::ranges::upper_bound(steps, stage) - steps.begin());
}

}  // namespace

int growthStage(int sexCount)
{
    return milestones(thresholds, sexCount);
}

Target calculate(Group group, int sexCount)
{
    const bool decay = group == Group::WeaponDecay || group == Group::AccessoryDecay;
    const int growth = growthStage(sexCount);
    const int stage = decay ? 40 - growth : growth;
    Target result{};
    result.stage = stage;
    if (group == Group::WeaponGrowth || group == Group::WeaponDecay) {
        result.weapon = {weighted(stage, 30, 45, 25), weighted(stage, 20, 25, 45),
                         general(stage, 100), general(stage, 100), milestones(stealSteps, stage)};
    } else {
        result.accessory = {general(stage, 30), general(stage, 100), general(stage, 100),
                            general(stage, 50), general(stage, 50), general(stage, 10),
                            general(stage, 30), general(stage, 30), general(stage, 30),
                            milestones(absorbSteps, stage)};
    }
    return result;
}

GroupTargets targets(std::span<const Group> equipped, std::optional<int> sexCount)
{
    GroupTargets result{};
    if (sexCount && *sexCount >= 0) {
        for (const auto group : equipped) {
            result[static_cast<std::size_t>(group)] = calculate(group, *sexCount);
        }
    }
    return result;
}

std::uint64_t StatLogger::beginSession()
{
    previous_.reset();
    return ++session_;
}

void StatLogger::observe(std::uint64_t session, Event event, Snapshot sample,
                         std::span<const Group> equipped, bool compare)
{
    if (session == 0 || session != session_) {
        return;
    }
    if (sample.status == ReadStatus::Valid &&
        std::ranges::any_of(sample.values, [](int value) { return value < 0; })) {
        sample.status = ReadStatus::Error;
    }

    using namespace std::chrono;
    const auto timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    out_ << "TDS_STATS seq=" << ++sequence_ << " session=" << session_
         << " utc_ms=" << timestamp.count()
         << " event=" << events[static_cast<std::size_t>(event)]
         << " status=" << statuses[static_cast<std::size_t>(sample.status)];

    if (!sample.detail.empty()) {
        out_ << " detail=" << sample.detail;
    }
    if (sample.status != ReadStatus::Valid) {
        out_ << " baseline_preserved=" << previous_.has_value() << " effects=NOT_UPDATED\n";
        return;
    }

    const bool decreased = compare && previous_ && !std::ranges::equal(
        sample.values, *previous_, [](int current, int previous) { return current >= previous; });
    out_ << " baseline=" << (!compare || !previous_) << " decreased=" << decreased
         << " reset=" << (decreased ? "UNCONFIRMED" : "NO")
         << " delta_scope=OBSERVATION_INTERVAL";
    for (std::size_t i = 0; i < statNames.size(); ++i) {
        out_ << ' ' << statNames[i] << '=' << sample.values[i];
        if (compare && previous_) {
            out_ << " prev_" << statNames[i] << '=' << (*previous_)[i]
                 << " d_" << statNames[i] << '=' << sample.values[i] - (*previous_)[i];
        }
    }

    const auto planned = targets(equipped, sample.values[0]);
    out_ << " growth_stage=" << growthStage(sample.values[0]) << " effects=TARGET_ONLY";
    for (std::size_t i = 0; i < planned.size(); ++i) {
        if (planned[i]) {
            out_ << ' ' << groupNames[i] << "_stage=" << planned[i]->stage;
        }
    }
    out_ << '\n';
    previous_ = sample.values;
}

}  // namespace tds
