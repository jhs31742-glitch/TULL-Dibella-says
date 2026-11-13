#include "HardcoreState.h"
#include <cassert>
#include <iostream>

int main()
{
    tds::HardcoreState state;
    assert(!state.enabled && !state.observe(3));
    assert(!state.observe(5));
    state.setEnabled(true);
    assert(!state.observe(5));
    assert(state.observe(6) && state.delta == 1);
    assert(!state.observe(6) && state.delta == 0);
    assert(!state.observe(std::nullopt));
    assert(!state.observe(8) && !state.delta);
    assert(state.observe(9) && state.delta == 1);
    assert(!state.observe(0) && state.delta == -9);
    state.setEnabled(false);
    assert(!state.observe(11));
    state.setEnabled(true);
    assert(!state.observe(11));
    assert(state.observe(12));
    tds::HardcoreState loaded;
    loaded.enabled = true;
    assert(!loaded.observe(100));
    assert(loaded.observe(101));
    std::cout << "PASS hardcore V baseline, OFF/ON, duplicate, failure recovery and load\n";
}
