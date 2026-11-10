#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>

namespace tds {

inline constexpr std::array thresholds{
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 21,
    24, 27, 31, 35, 39, 44, 49, 56, 63, 73, 83, 97, 111, 130,
    149, 174, 199, 231, 263, 303, 343, 392, 441, 500
};

enum class Group { WeaponGrowth, WeaponDecay, AccessoryGrowth, AccessoryDecay };

struct Weapon {
    int flatDamage{}, damagePercent{}, health{}, stamina{}, lifeSteal{};
    bool operator==(const Weapon&) const = default;
};

struct Accessory {
    int armor{}, magicka{}, stamina{}, disease{}, poison{}, magic{};
    int fire{}, frost{}, shock{}, spellAbsorption{};
    bool operator==(const Accessory&) const = default;
};

struct Target {
    int stage{};
    Weapon weapon;
    Accessory accessory;
    bool operator==(const Target&) const = default;
};

[[nodiscard]] int growthStage(int sexCount);
[[nodiscard]] Target calculate(Group group, int sexCount);

// Eligible groups come from verified equipped instances. This computes targets only.
using GroupTargets = std::array<std::optional<Target>, 4>;
[[nodiscard]] GroupTargets targets(std::span<const Group> equipped, std::optional<int> sexCount);

inline constexpr std::array<std::string_view, 11> statNames{
    "SexCount", "M", "F", "C", "S", "A", "V", "VG", "AN", "OR", "PlayerSexRaw"
};
using Stats = std::array<int, statNames.size()>;
enum class ReadStatus { Valid, NotReady, Error, Stale };
enum class Event { Load, NewGame, SceneEnd, Refresh, UiOpen, Recheck };

struct Snapshot {
    ReadStatus status{ReadStatus::NotReady};
    Stats values{};
    std::string_view detail{};
};

// A Valid snapshot requires verified readiness and a complete, confirmed-fresh sample.
// The caller owns the provider and supplies its session token on asynchronous completion.
class StatLogger {
public:
    explicit StatLogger(std::ostream& out) : out_(out) {}
    [[nodiscard]] std::uint64_t beginSession();
    void observe(std::uint64_t session, Event event, Snapshot sample,
                 std::span<const Group> equipped, bool compare = true);
    [[nodiscard]] const std::optional<Stats>& baseline() const { return previous_; }

private:
    std::ostream& out_;
    std::uint64_t session_{};
    std::uint64_t sequence_{};
    std::optional<Stats> previous_;
};

}  // namespace tds
