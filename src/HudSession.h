#pragma once
#include <atomic>
#include <string>
namespace tds::panel {
struct HudSession {
    std::atomic_bool enabled{};
    std::string layout="0,84,70,100,0";
    void reset() { enabled=false; layout="0,84,70,100,0"; }
};
}
