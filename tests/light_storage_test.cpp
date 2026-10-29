#include <cassert>
#include <cstdio>
#include <Preferences.h>
#include "light_storage.h"

using namespace light;
State restored;
bool normal = false;
void reboot(Id id) {
    restored = {};
    normal = false;
    beginLightStorage();
    assert(loadLightState(id, restored, normal));
}
int main() {
    beginLightStorage();
    assert(!loadLightState(Id::Kitchen, restored, normal));
    State kitchen{Power::On, 2, -1}, living{Power::Night, 3, 4};
    assert(saveLightState(Id::Kitchen, kitchen, false));
    assert(saveLightState(Id::Living, living, true));
    reboot(Id::Kitchen);
    assert(restored.power == Power::On && restored.brightness == 2);
    reboot(Id::Living);
    assert(restored.power == Power::Night && restored.brightness == 3 && restored.color == 4 && normal);

    Plan plan;
    plan.signals[0] = Signal::Cycle; plan.count = 1;
    assert(prepareLightState(Id::Kitchen, kitchen, false, plan));
    reboot(Id::Kitchen);
    assert(restored.power == Power::Unknown && restored.brightness == 2);
    plan.signals[0] = Signal::Up;
    assert(prepareLightState(Id::Kitchen, kitchen, false, plan));
    reboot(Id::Kitchen);
    assert(restored.power == Power::On && restored.brightness == -1);
    plan.signals[1] = Signal::Cycle; plan.count = 2;
    assert(prepareLightState(Id::Kitchen, kitchen, false, plan));
    reboot(Id::Kitchen);
    assert(restored.power == Power::Unknown && restored.brightness == -1);

    Plan calibration;
    calibration.count = kSyncPresses;
    calibration.syncLevel = kMaxStep;
    for (int i = 0; i < calibration.count; ++i) calibration.signals[i] = Signal::Up;
    assert(prepareLightState(Id::Kitchen, {Power::Off, 2, -1}, false, calibration));
    reboot(Id::Kitchen);
    assert(restored.power == Power::On && restored.brightness == -1);

    // Other fixture remains intact, including its saved preset indicator.
    reboot(Id::Living);
    assert(normal && restored.color == 4);
    plan.count = 1;
    for (Signal signal : {Signal::Up, Signal::Down, Signal::Warm, Signal::Cool,
                          Signal::Off, Signal::Night, Signal::On, Signal::Normal, Signal::Full}) {
        plan.signals[0] = signal;
        assert(prepareLightState(Id::Living, living, true, plan));
        reboot(Id::Living);
        if (isAdjustment(signal)) {
            assert(restored.power == living.power);
            assert(restored.brightness == ((signal == Signal::Up || signal == Signal::Down) ? -1 : 3));
            assert(restored.color == ((signal == Signal::Warm || signal == Signal::Cool) ? -1 : 4));
            assert(!normal);
        } else {
            assert(restored.power == Power::Unknown);
            bool retains = signal == Signal::Off || signal == Signal::Night;
            assert(restored.brightness == (retains ? 3 : -1));
            assert(restored.color == (retains ? 4 : -1));
            assert(normal == retains);
        }
    }
    // On + adjustment interrupted at any point invalidates both preset axes.
    plan = planLivingAxis({Power::Off, 3, 4}, Axis::Brightness, 2);
    assert(prepareLightState(Id::Living, living, true, plan));
    reboot(Id::Living);
    assert(restored.power == Power::Unknown && restored.brightness == -1 && restored.color == -1);
    // Failed final write leaves the conservative pre-send snapshot intact.
    fakeWriteFailure = true;
    assert(!saveLightState(Id::Living, living, true));
    assert(!prepareLightState(Id::Kitchen, kitchen, false, plan));
    reboot(Id::Living);
    assert(restored.power == Power::Unknown);
    fakeWriteFailure = false;
    assert(saveLightState(Id::Living, living, true));
    reboot(Id::Living);
    assert(restored.power == living.power && restored.brightness == 3 && normal);
    // A new firmware process uses the same versioned bytes, never queued commands.
    fakeNvs["living"] = "bad";
    assert(!loadLightState(Id::Living, restored, normal));
    for (std::string bytes : {std::string{2, 1, 1, 1, 0}, std::string{1, 9, 1, 1, 0},
                              std::string{1, 1, 9, 1, 0}, std::string{1, 1, 1, 9, 0},
                              std::string{1, 1, 1, 1, 2}}) {
        fakeNvs["living"] = bytes;
        assert(!loadLightState(Id::Living, restored, normal));
    }
    puts("light_storage tests passed");
}
