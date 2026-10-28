#pragma once
#include <cstdint>

namespace light {
enum class Power { Unknown, On, Night, Off };
enum class Signal { Cycle, Up, Down };
struct State {
    Power power = Power::Unknown;
    int brightness = -1;  // Estimated step: 0..4 = 20..100, -1 = unknown.
};
constexpr int kMaxStep = 4;
constexpr int kSyncPresses = kMaxStep;  // Five levels need four presses end to end.

inline int pressesTo(Power from, Power to) {
    if (from == Power::Unknown || to == Power::Unknown) return -1;
    return (static_cast<int>(to) - static_cast<int>(from) + 3) % 3;
}
inline void apply(State& state, Signal signal) {
    if (signal == Signal::Cycle) {
        if (state.power != Power::Unknown)
            state.power = static_cast<Power>(static_cast<int>(state.power) % 3 + 1);
        // The fixture retains its brightness through night/off states.
    } else if (state.power == Power::On && state.brightness >= 0) {
        if (signal == Signal::Up && state.brightness < kMaxStep) ++state.brightness;
        if (signal == Signal::Down && state.brightness > 0) --state.brightness;
    }
}
inline void uncertain(State& state, Signal signal) {
    if (signal == Signal::Cycle) state.power = Power::Unknown;
    else state.brightness = -1;
}

struct Plan {
    Signal signals[6]{};  // At most two power presses and four brightness presses.
    int count = 0;
    int syncLevel = -1;
    bool valid = true;
};
inline int alexaBrightnessStep(uint8_t value) {
    int level = (static_cast<int>(value) * 5 + 127) / 255;
    return level > 0 ? level - 1 : 0;
}
inline Plan planTarget(State state, Power desired, int brightness = -1) {
    Plan plan;
    int powerPresses = pressesTo(state.power, desired);
    if (powerPresses < 0 || brightness < -1 || brightness > kMaxStep ||
        (brightness >= 0 && (desired != Power::On || state.brightness < 0))) {
        plan.valid = false;
        return plan;
    }
    for (int i = 0; i < powerPresses; ++i) plan.signals[plan.count++] = Signal::Cycle;
    if (brightness >= 0) {
        int diff = brightness - state.brightness;
        Signal signal = diff > 0 ? Signal::Up : Signal::Down;
        for (int i = 0; i < (diff < 0 ? -diff : diff); ++i) plan.signals[plan.count++] = signal;
    }
    return plan;
}
}  // namespace light
