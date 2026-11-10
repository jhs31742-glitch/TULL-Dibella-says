#pragma once

#include <optional>

namespace tds {

struct HardcoreState {
    bool enabled{};
    std::optional<int> lastV;
    std::optional<int> delta;

    void setEnabled(bool value)
    {
        if (enabled != value) {
            enabled = value;
            lastV.reset();
            delta.reset();
        }
    }

    bool observe(std::optional<int> value)
    {
        delta.reset();
        if (value && lastV) {
            delta = *value - *lastV;
        }
        lastV = value;
        return enabled && delta && *delta > 0;
    }
};

}  // namespace tds
