#pragma once

#include "SexLabStats.h"

#include <memory>

namespace tds {

class PendingRead;

// Call on the game thread only after verifying the effective provider and
// player readiness. Drop the handle on timeout, revert, or a new session.
[[nodiscard]] std::shared_ptr<PendingRead> beginPlayerRead(std::string_view verifiedSha);
[[nodiscard]] std::optional<Snapshot> completedRead(const std::shared_ptr<PendingRead>& read);

}  // namespace tds
