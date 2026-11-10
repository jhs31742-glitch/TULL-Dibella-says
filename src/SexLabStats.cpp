#include "SexLabStats.h"

#include <cmath>
#include <limits>

namespace tds {

Snapshot decodeNativeStats(std::string_view verifiedSha, ReadStatus status,
                           const NativeValues& values)
{
    if (status != ReadStatus::Valid) {
        return {status, {}};
    }
    if (verifiedSha != frameworkStatsSha && verifiedSha != utilityPlusStatsSha) {
        return {ReadStatus::NotReady, {}};
    }

    Stats result{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = values[i];
        if (!value || !std::isfinite(*value) || *value < 0 ||
            static_cast<double>(*value) > std::numeric_limits<int>::max()) {
            return {ReadStatus::Error, {}};
        }
        result[i] = static_cast<int>(*value);
    }
    return {ReadStatus::Valid, result};
}

}  // namespace tds
