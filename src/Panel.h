#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <charconv>

namespace tds::panel {

enum class Action { Open, Refresh, HardcoreOn, HardcoreOff, Recheck, NodesOn, NodesOff, NodesReset, NodeBuy, DeveloperEnter, DeveloperInput, AuraOn, AuraOff, GearGrant };

[[nodiscard]] inline std::optional<unsigned> parseNodePurchase(std::string_view input)
{
    constexpr std::string_view prefix="node-buy:";
    if (!input.starts_with(prefix)) return std::nullopt;
    input.remove_prefix(prefix.size());
    if (input.empty() || input.size()>2) return std::nullopt;
    unsigned node{};
    const auto end=input.data()+input.size();
    const auto [ptr,ec]=std::from_chars(input.data(),end,node);
    if (ec!=std::errc{} || ptr!=end) return std::nullopt;
    return node;
}

void install(std::string (*snapshot)(), void (*action)(Action, unsigned, std::string_view), void (*log)(const char*));
void publish();
void close();
void resetSession();
bool hudTickNeeded();
void tickHud();

}
