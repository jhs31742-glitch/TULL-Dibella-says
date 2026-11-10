#pragma once

#include <cmath>

namespace tds {

enum class ResetResult { Applied, InvalidSnapshot, RolledBack, RollbackFailed };

struct MagickaLedger {
    float base;
    float growth;
    float xp;
    bool operator==(const MagickaLedger&) const = default;
};

// Caller must establish exclusive access to the state before entering.
template <class Read, class Write>
ResetResult resetMagickaLedger(Read read, Write write)
{
    const auto before = read();
    if (!std::isfinite(before.base) || !std::isfinite(before.growth) ||
        !std::isfinite(before.xp) || before.base < 0 || before.growth < 0 || before.xp < 0) {
        return ResetResult::InvalidSnapshot;
    }
    constexpr MagickaLedger reset{10, 0, 0};
    write(reset);
    if (read() == reset) return ResetResult::Applied;
    write(before);
    return read() == before ? ResetResult::RolledBack : ResetResult::RollbackFailed;
}

}  // namespace tds
