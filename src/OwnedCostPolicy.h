#pragma once
#include <cmath>
namespace tds::progression {
template<class Has, class Add>
bool synchronizeOwnedCost(float& value,int basisPoints,Has has,Add add) {
    const float factor=1.0f+static_cast<float>(basisPoints)/10000.0f;
    if (!std::isfinite(factor) || factor<=0.0f) return false;
    const float previous=value;
    value=factor;
    // The entry point reads this owned one-value data. Neutralize without perk churn.
    if (has() || basisPoints==0) return true;
    add();
    if (has()) return true;
    value=previous;
    return false;
}
}
