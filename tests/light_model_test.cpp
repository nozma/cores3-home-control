#include "light_model.h"
#include <cassert>
#include <initializer_list>

int main() {
    using namespace light;
    for (Power from : {Power::On, Power::Night, Power::Off}) {
        for (Power to : {Power::On, Power::Night, Power::Off}) {
            State state{from, 2};
            int count = pressesTo(from, to);
            assert(count >= 0 && count <= 2);
            for (int i = 0; i < count; ++i) apply(state, Signal::Cycle);
            assert(state.power == to);
            assert(state.brightness == 2);
        }
    }
    assert(pressesTo(Power::Off, Power::On) == 1);
    assert(pressesTo(Power::On, Power::Off) == 2);
    assert(pressesTo(Power::Unknown, Power::On) == -1);
    State state;
    apply(state, Signal::Cycle);
    assert(state.power == Power::Unknown);
    state = {Power::On, 0};
    for (int i = 0; i < kSyncPresses; ++i) apply(state, Signal::Up);
    assert(state.brightness == kMaxStep);
    for (int i = 0; i < kSyncPresses; ++i) apply(state, Signal::Down);
    assert(state.brightness == 0);
    uncertain(state, Signal::Up);
    assert(state.power == Power::On && state.brightness == -1);
    apply(state, Signal::Up);
    assert(state.brightness == -1);
    uncertain(state, Signal::Cycle);
    assert(state.power == Power::Unknown && state.brightness == -1);
    state = {Power::On, 3};
    uncertain(state, Signal::Cycle);
    assert(state.power == Power::Unknown && state.brightness == 3);
    for (Power power : {Power::Night, Power::Off, Power::Unknown}) {
        state = {power, 2};
        apply(state, Signal::Up);
        apply(state, Signal::Down);
        assert(state.brightness == 2);
    }
}
