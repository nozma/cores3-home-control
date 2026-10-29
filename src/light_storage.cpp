#include "light_storage.h"
#include <Preferences.h>

namespace {
Preferences storage;
bool ready = false;
// Explicit bytes keep the on-flash format independent of enum size and padding.
constexpr uint8_t kVersion = 1;
const char* key(light::Id id) { return id == light::Id::Kitchen ? "kitchen" : "living"; }
}

void beginLightStorage() { ready = storage.begin("light-state", false); }

bool loadLightState(light::Id id, light::State& state, bool& normal) {
    uint8_t bytes[5];
    if (!ready || storage.getBytesLength(key(id)) != sizeof(bytes) ||
        storage.getBytes(key(id), bytes, sizeof(bytes)) != sizeof(bytes)) return false;
    const int max = id == light::Id::Kitchen ? light::kMaxStep : light::kLivingMax;
    if (bytes[0] != kVersion || bytes[1] > static_cast<uint8_t>(light::Power::Off) ||
        bytes[2] > max + 1 || bytes[3] > light::kLivingMax + 1 || bytes[4] > 1 ||
        (id == light::Id::Kitchen && (bytes[3] != 0 || bytes[4] != 0))) return false;
    state.power = static_cast<light::Power>(bytes[1]);
    state.brightness = int(bytes[2]) - 1;
    state.color = int(bytes[3]) - 1;
    normal = bytes[4];
    return true;
}

bool saveLightState(light::Id id, const light::State& state, bool normal) {
    const uint8_t bytes[] = {kVersion, static_cast<uint8_t>(state.power),
        static_cast<uint8_t>(state.brightness + 1), static_cast<uint8_t>(state.color + 1),
        static_cast<uint8_t>(normal)};
    // NVS commits a blob atomically; a failed final save leaves the safe snapshot.
    return ready && storage.putBytes(key(id), bytes, sizeof(bytes)) == sizeof(bytes);
}

bool prepareLightState(light::Id id, light::State state, bool normal, const light::Plan& plan) {
    using namespace light;
    // Kitchen calibration explicitly confirms that the fixture is already on.
    if (id == Id::Kitchen && plan.syncLevel >= 0) state.power = Power::On;
    for (int i = 0; i < plan.count; ++i) {
        const Signal signal = plan.signals[i];
        if (id == Id::Kitchen) uncertain(state, signal);
        else if (isAdjustment(signal)) {
            if (signal == Signal::Up || signal == Signal::Down) state.brightness = -1;
            else state.color = -1;
            normal = false;
        } else {
            state.power = Power::Unknown;
            if (signal == Signal::On || signal == Signal::Normal || signal == Signal::Full) {
                state.brightness = state.color = -1;
                normal = false;
            }
        }
    }
    return saveLightState(id, state, normal);
}
