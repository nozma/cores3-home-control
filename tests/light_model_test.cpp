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
    for (Power power : {Power::On, Power::Night, Power::Off}) {
        for (int from = 0; from <= kMaxStep; ++from) {
            for (int to = 0; to <= kMaxStep; ++to) {
                state = {power, from};
                auto plan = planTarget(state, Power::On, to);
                assert(plan.valid && plan.count <= 6);
                for (int i = 0; i < plan.count; ++i) apply(state, plan.signals[i]);
                assert(state.power == Power::On && state.brightness == to);
            }
        }
    }
    assert(!planTarget({Power::Unknown, 4}, Power::On).valid);
    assert(!planTarget({Power::On, -1}, Power::On, 3).valid);
    assert(planTarget({Power::On, -1}, Power::Off).valid);
    assert(planTarget({Power::Off, 2}, Power::Off).count == 0);
    assert(alexaBrightnessStep(1) == 0);
    assert(alexaBrightnessStep(51) == 0);
    assert(alexaBrightnessStep(102) == 1);
    assert(alexaBrightnessStep(153) == 2);
    assert(alexaBrightnessStep(204) == 3);
    assert(alexaBrightnessStep(255) == 4);
    // Living has direct commands, even when the power is initially unknown.
    auto living = planLiving({Power::Unknown, -1}, Power::Off, -1);
    assert(living.valid && living.count == 1 && living.signals[0] == Signal::Off);
    living = planLiving({Power::Unknown, -1}, Power::Night, -1);
    assert(living.valid && living.signals[0] == Signal::Night);
    assert(planLiving({Power::On, -1}, Power::On, 2).valid);
    living = planLiving({Power::On, -1}, Power::On);
    assert(living.valid && living.count == 0);
}
