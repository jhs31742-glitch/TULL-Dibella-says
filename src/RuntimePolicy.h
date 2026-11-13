#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tds::runtime {
struct Version {
    unsigned major, minor, build, revision;
    constexpr bool operator==(const Version&) const = default;
};
// Released SKSE/Address Library runtime families. No inferred future-version ABI.
inline constexpr std::array supported{
    Version{1,5,97,0}, Version{1,6,318,0}, Version{1,6,323,0},
    Version{1,6,342,0}, Version{1,6,353,0}, Version{1,6,629,0},
    Version{1,6,640,0}, Version{1,6,659,0}, Version{1,6,1130,0},
    Version{1,6,1170,0}, Version{1,6,1179,0}
};
constexpr bool accepts(Version value) {
    for (auto version : supported) if (value==version) return true;
    return false;
}
struct HitSite {
    std::uint64_t id;
    std::size_t offset;
    bool ae;
};
constexpr HitSite hitSite(Version version) {
    if (!accepts(version)) return {};
    return version.minor==6 ? HitSite{38586,0xFA,true} : HitSite{37633,0x16A,false};
}
inline constexpr std::size_t healthDamageSlot=0x104;
inline constexpr std::size_t modifyValueSlot=0x06;
inline constexpr std::size_t spellValueSlot=0x20;
}
