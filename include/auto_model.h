#pragma once
#include <cstdint>
#include "light_model.h"
namespace automation {
struct Settings { bool enabled = false; int wake = 390, work = 1020, bed = 1380; };
inline bool valid(const Settings& s) {
    return s.wake >= 300 && s.wake <= 540 && s.work >= 900 && s.work <= 1140 &&
        s.bed >= 1260 && s.bed <= 1440 && s.wake % 15 == 0 && s.work % 15 == 0 && s.bed % 15 == 0;
}
enum class Phase { Off, Dawn, Day, Evening, Late, Night };
inline Phase phase(const Settings& s, int minute) {
    if (minute < s.wake) return minute < s.bed + 15 - 1440 ? Phase::Night : Phase::Off;
    if (minute < s.wake + 60) return Phase::Dawn;
    if (minute >= s.bed + 15) return Phase::Off;
    if (minute >= s.bed) return Phase::Night;
    if (minute >= s.bed - 120) return Phase::Late;
    if (minute >= s.work) return Phase::Evening;
    return Phase::Day;
}
// Advance only forwards: boot, missed minutes and clock rollback never replay events.
inline bool due(int64_t now, int64_t& highWater, bool force, bool isEvent) {
    if (now < 0) return false;
    bool fire = force || (highWater >= 0 && now == highWater + 1 && isEvent);
    if (now > highWater) highWater = now;
    return fire;
}
inline int ramp(int start, int maximum, int elapsed) {
    return start + (maximum - start) * elapsed / 60;
}
// Fade 40% to minimum over 105 minutes; hold minimum until bedtime.
inline int bedtimeLevel(const Settings& s, int minute, light::Id id) {
    int elapsed = minute - (s.bed - 120);
    if (elapsed < 0) elapsed = 0;
    if (elapsed > 105) elapsed = 105;
    int start = id == light::Id::Kitchen ? 1 : 2;
    return start - start * elapsed / 105;
}
// A minute is an event only when a physical level changes. The final step uses Full.
inline bool event(const Settings& s, int m, int kitchen, int brightness, int color) {
    if (m == s.wake || m == s.wake + 60 || m == s.work || m == s.bed - 120 ||
        m == s.bed % 1440 || m == (s.bed + 15) % 1440) return true;
    if (m > s.bed - 120 && m <= s.bed - 15) {
        return bedtimeLevel(s,m,light::Id::Kitchen) != bedtimeLevel(s,m-1,light::Id::Kitchen) ||
            bedtimeLevel(s,m,light::Id::Living) != bedtimeLevel(s,m-1,light::Id::Living);
    }
    if (m <= s.wake || m >= s.wake + 60) return false;
    int e = m - s.wake;
    return ramp(kitchen, 4, e) != ramp(kitchen, 4, e-1) ||
        ramp(brightness, 5, e) != ramp(brightness, 5, e-1) ||
        ramp(color, 5, e) != ramp(color, 5, e-1);
}
}

namespace automation {
inline light::Plan plan(light::Id id, light::State state, light::Power power,
                        int brightness, int color, bool full, bool restore,
                        int savedBrightness, int savedColor) {
    using namespace light;
    if (id==Id::Kitchen) return planTarget(state,power,brightness);
    if (power!=Power::On) return planLiving(state,power);
    Plan job;
    if (full) { job.signals[job.count++]=Signal::Full; return job; }
    if (brightness<0 || brightness>5 || color<0 || color>5) { job.valid=false; return job; }
    if (state.power!=Power::On || (restore && (state.brightness<0 || state.color<0))) {
        job.signals[job.count++]=Signal::Normal;
        state.power=Power::On;
        state.brightness=restore?savedBrightness:-1;
        state.color=restore?savedColor:-1;
    }
    auto append=[&](Axis axis,int target) {
        auto axisPlan=planLivingAxis(state,axis,target);
        for(int i=0;i<axisPlan.count;++i) job.signals[job.count++]=axisPlan.signals[i];
        if(axis==Axis::Brightness) state.brightness=target;
        else state.color=target;
    };
    append(Axis::Brightness,brightness);
    append(Axis::Color,color);
    job.syncLevel=brightness; job.syncColor=color;
    return job;
}
}
