#include "light_model.h"
#include <cassert>
#include <initializer_list>

int main() {
    using namespace light;
    for (Axis axis : {Axis::Brightness, Axis::Color}) {
        for (int current = -1; current <= 5; ++current) {
            for (int target = 0; target <= 5; ++target) {
                State state;
                state.power = Power::On;
                state.brightness = state.color = current;
                auto plan = planLivingAxis(state, axis, target);
                assert(plan.valid && plan.count <= 8);
                if (target == 0 || target == 5) {
                    assert(plan.count == 6); // Always recalibrate, even at the same endpoint.
                    for (int i = 0; i < 6; ++i)
                        assert(plan.signals[i] == (axis == Axis::Brightness ?
                            (target == 0 ? Signal::Down : Signal::Up) :
                            (target == 0 ? Signal::Warm : Signal::Cool)));
                } else if (current >= 0) {
                    assert(plan.count == (target > current ? target - current : current - target));
                } else {
                    assert(plan.count == 6 + (target <= 2 ? target : 5 - target));
                }
                // Simulate every possible real starting position, including unknown estimates.
                for (int actual = 0; actual <= 5; ++actual) {
                    if (current >= 0 && actual != current) continue;
                    State physical;
                    physical.power = Power::On;
                    physical.brightness = physical.color = actual;
                    for (int i = 0; i < plan.count; ++i) applyLiving(physical, plan.signals[i]);
                    assert((axis == Axis::Brightness ? physical.brightness : physical.color) == target);
                }
                assert((axis == Axis::Brightness ? plan.syncLevel : plan.syncColor) == target);
            }
        }
    }
    State state;
    state.power = Power::Off;
    state.brightness = state.color = 3;
    auto plan = planLivingAxis(state, Axis::Brightness, 2);
    assert(plan.signals[0] == Signal::On && plan.count == 9);
    assert(!planLivingAxis(state, Axis::Brightness, 6).valid);
    state.power = Power::On;
    uncertainLiving(state, Signal::Cool);
    assert(state.color == -1 && state.brightness == 3 && state.power == Power::On);
    state.color = 2;
    uncertainLiving(state, Signal::Down);
    assert(state.brightness == -1 && state.color == 2);
    applyLiving(state, Signal::Full);
    assert(state.brightness == 5 && state.color == -1 && state.power == Power::On);
    applyLiving(state, Signal::Off);
    assert(state.brightness == 5 && state.power == Power::Off);
    assert(livingStep(1) == 0 && livingStep(102) == 2 && livingStep(255) == 5);
}
