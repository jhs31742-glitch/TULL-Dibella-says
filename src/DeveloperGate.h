#pragma once
#include <string_view>
namespace tds::progression {
[[nodiscard]] constexpr bool developerPasswordAccepted(std::string_view value) {
    if (value.size()!=4) return false;
    constexpr std::string_view expected="tull";
    for (unsigned i=0;i<4;++i) {
        char c=value[i]; if (c>='A' && c<='Z') c=char(c-'A'+'a');
        if (c!=expected[i]) return false;
    }
    return true;
}
}
