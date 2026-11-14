#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tds {
inline constexpr std::uint32_t settingsRecord=0x31544553; // SET1
using SaveSettings=std::array<std::uint32_t,8>;
// Packed keyboard shortcut, HUD enabled/direction/X/Y/scale/style, aura enabled.
inline constexpr SaveSettings defaultSettings{290,0,0,84,70,100,0,0};
constexpr bool validSettings(const SaveSettings& s) {
    const auto code=s[0]&255u;
    return s[0]<2048 && code>0 && code!=42 && code!=54 && code!=29 &&
        code!=157 && code!=56 && code!=184 && s[1]<=1 && s[2]<=1 &&
        s[3]<=100 && s[4]<=100 && s[5]>=50 && s[5]<=160 && s[6]<=1 && s[7]<=1;
}
inline std::optional<SaveSettings> decodeSettings(std::uint32_t version,std::uint32_t length,
    std::uint32_t read,const SaveSettings& value,bool seen) {
    if (seen || version!=1 || length!=sizeof(value) || read!=sizeof(value) || !validSettings(value))
        return std::nullopt;
    return value;
}
inline std::string hudLayout(const SaveSettings& s) {
    return std::to_string(s[2])+","+std::to_string(s[3])+","+std::to_string(s[4])+","+
           std::to_string(s[5])+","+std::to_string(s[6]);
}
inline bool captureHudLayout(std::string_view text,SaveSettings& settings) {
    auto value=settings;
    for (unsigned i=2;i<=6;++i) {
        if (text.empty()) return false;
        const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value[i]);
        if (error!=std::errc{}) return false;
        if (i==6 ? end!=text.data()+text.size() : end==text.data()+text.size() || *end!=',') return false;
        text.remove_prefix(end-text.data()+(i==6 ? 0:1));
    }
    if (!validSettings(value)) return false;
    settings=value;
    return true;
}
// Return no record for QA sessions. Missing/invalid/old saves use defaults,
// never the settings from whichever save was loaded immediately before them.
inline std::optional<SaveSettings> settingsForSave(bool developer,const SaveSettings& settings) {
    return !developer && validSettings(settings) ? std::optional{settings} : std::nullopt;
}
inline SaveSettings settingsForLoad(const std::optional<SaveSettings>& saved,bool loading,
                                    const SaveSettings& defaults) {
    return loading && saved && validSettings(*saved) ? *saved : defaults;
}
}
