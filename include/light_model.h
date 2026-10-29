#pragma once
#include <cstdint>

namespace light {
enum class Power { Unknown, On, Night, Off };
enum class Id { Kitchen = 0, Living = 1 };
enum class Signal { Cycle, Up, Down, On, Off, Night, Warm, Cool, Full, Normal };
struct State {
    Power power = Power::Unknown;
    int brightness = -1;  // Kitchen: 0..4 = 20..100%; living: 0..5 = 0..100%.
    int color = -1;  // Living: 0..5 = warm..cool, -1 = unknown.
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
    Signal signals[32]{};
    int count = 0;
    int syncLevel = -1;
    int syncColor = -1;
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
enum class Axis { Brightness, Color };
constexpr int kLivingMax = 5;
constexpr int kLivingCalibrationSets = 5;
inline bool isAdjustment(Signal signal) {
    return signal == Signal::Up || signal == Signal::Down || signal == Signal::Warm || signal == Signal::Cool;
}
inline int livingStep(uint8_t value) {
    return (static_cast<int>(value) * 5 + 127) / 255;
}
inline Plan planLivingAxis(State state, Axis axis, int target) {
    Plan plan;
    if (target < 0 || target > kLivingMax) { plan.valid = false; return plan; }
    int current = axis == Axis::Brightness ? state.brightness : state.color;
    Signal up = axis == Axis::Brightness ? Signal::Up : Signal::Cool;
    Signal down = axis == Axis::Brightness ? Signal::Down : Signal::Warm;
    if (state.power != Power::On) {
        plan.signals[plan.count++] = Signal::On;
        current = -1; // "Normal" restores the fixture's saved settings.
    }
    if (current < 0) {
        int reference = target <= 2 ? 0 : kLivingMax;
        for (int i = 0; i < kLivingCalibrationSets; ++i)
            plan.signals[plan.count++] = reference == 0 ? down : up;
        current = reference;
    }
    int diff = target - current;
    for (int i = 0; i < (diff < 0 ? -diff : diff); ++i)
        plan.signals[plan.count++] = diff > 0 ? up : down;
    if (axis == Axis::Brightness) plan.syncLevel = target;
    else plan.syncColor = target;
    return plan;
}
inline Plan planLiving(State state, Power desired, int brightness = -1) {
    if (brightness >= 0) {
        if (desired == Power::On) return planLivingAxis(state, Axis::Brightness, brightness);
        Plan invalid; invalid.valid = false; return invalid;
    }
    Plan plan;
    if (desired == Power::Unknown) { plan.valid = false; return plan; }
    if (state.power != desired)
        plan.signals[plan.count++] = desired == Power::On ? Signal::On :
            desired == Power::Off ? Signal::Off : Signal::Night;
    return plan;
}
inline void applyLiving(State& state, Signal signal) {
    if (signal == Signal::Full) {
        state.power = Power::On;
        state.brightness = kLivingMax;
        state.color = kLivingMax; // Full light is treated as the whitest endpoint.
    } else if (signal == Signal::On || signal == Signal::Normal) {
        state.power = Power::On;
        state.brightness = state.color = -1;
    } else if (signal == Signal::Off || signal == Signal::Night) {
        state.power = signal == Signal::Off ? Power::Off : Power::Night;
    } else if (state.power == Power::On && isAdjustment(signal)) {
        int& position = signal == Signal::Up || signal == Signal::Down ? state.brightness : state.color;
        if (position >= 0) {
            position += signal == Signal::Up || signal == Signal::Cool ? 1 : -1;
            if (position < 0) position = 0;
            if (position > kLivingMax) position = kLivingMax;
        }
    }
}
inline void uncertainLiving(State& state, Signal signal) {
    if (!isAdjustment(signal)) {
        state.power = Power::Unknown;
        state.brightness = state.color = -1;
    } else if (signal == Signal::Up || signal == Signal::Down) state.brightness = -1;
    else state.color = -1;
}
}  // namespace light
