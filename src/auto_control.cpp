#include "auto_control.h"
#include "lighting.h"
#include <Preferences.h>
#include <ctime>
namespace {
using namespace automation;
Settings settings;
Preferences prefs;
bool ready = false, immediate = false, livingNightKnown = false;
int saved[3] = {1, 2, 0}; // Kitchen 40%, living 40%, warmest.
int64_t highWater = -1;
bool pending[2]{}, capture[2]{};
int64_t deadline = 0;
String outcomes[2];
String status = "自動制御は無効です";
int64_t minuteNow() { auto t = time(nullptr); return t < 1704067200 ? -1 : (int64_t(t) + 9 * 3600) / 60; }
bool saveNight(int n) {
    auto s = getLightingState(static_cast<light::Id>(n));
    if (n == 0 && s.brightness >= 0) saved[0] = s.brightness;
    if (n == 1) {
        if (s.brightness >= 0) saved[1] = s.brightness;
        if (s.color >= 0) saved[2] = s.color;
    }
    if (n == 1) livingNightKnown = s.brightness >= 0 && s.color >= 0;
    uint8_t bytes[] = {uint8_t(saved[0]), uint8_t(saved[1]), uint8_t(saved[2]), uint8_t(livingNightKnown)};
    return ready && prefs.putBytes("night", bytes, sizeof(bytes)) == sizeof(bytes);
}
}
const automation::Settings& automationSettings() { return settings; }
void beginAutomation() {
    ready = prefs.begin("automation", false);
    uint16_t data[4];
    if (ready && prefs.getBytesLength("settings") == sizeof(data) && prefs.getBytes("settings", data, sizeof(data)) == sizeof(data)) {
        Settings loaded; loaded.enabled=data[0]==1; loaded.wake=data[1]; loaded.work=data[2]; loaded.bed=data[3];
        if (data[0] <= 1 && valid(loaded)) settings = loaded;
    }
    uint8_t bytes[4];
    if (ready && prefs.getBytesLength("night") == 4 && prefs.getBytes("night", bytes, 4) == 4 && bytes[0] <= 4 && bytes[1] <= 5 && bytes[2] <= 5 && bytes[3] <= 1) {
        for (int i=0; i<3; ++i) saved[i] = bytes[i];
        livingNightKnown = bytes[3];
    }
    status = settings.enabled ? "時刻同期後、次の予定から実行" : "自動制御は無効です";
}
bool changeAutomation(int field, int direction) {
    Settings next = settings;
    if (field == 0) next.enabled = !next.enabled;
    else {
        int& value = field == 1 ? next.wake : field == 2 ? next.work : next.bed;
        value += direction * 15;
    }
    if (!valid(next)) return false;
    uint16_t data[] = {uint16_t(next.enabled), uint16_t(next.wake), uint16_t(next.work), uint16_t(next.bed)};
    if (!ready || prefs.putBytes("settings", data, sizeof(data)) != sizeof(data)) { status = "設定を保存できません"; return false; }
    immediate = next.enabled && (immediate || !settings.enabled);
    settings = next;
    pending[0] = pending[1] = false;
    outcomes[0] = outcomes[1] = "";
    // Settings edits take effect at the next event; enabling applies the current phase.
    highWater = minuteNow();
    status = settings.enabled ? "次の予定を待っています" : "自動制御は無効です";
    redrawLighting();
    return true;
}
void automationManual(light::Id id) {
    int n = static_cast<int>(id);
    if (pending[n]) outcomes[n] = "手動優先";
    pending[n] = false;
}
String automationStatus() {
    if (settings.enabled && (outcomes[0].length() || outcomes[1].length()))
        return "台所:" + outcomes[0] + " 居間:" + outcomes[1];
    return status;
}
void automationResult(light::Id id, bool success) {
    outcomes[static_cast<int>(id)] = success ? "完了" : "失敗";
}

String automationNext() {
    if (!settings.enabled) return "次回：無効";
    auto now = minuteNow();
    if (now < 0) return "次回：時刻同期待ち";
    for (int d=1; d<=1440; ++d) {
        int m = (now+d)%1440;
        if (event(settings,m,saved[0],saved[1],saved[2])) {
            char text[40]; snprintf(text,sizeof(text),"次回 %s%02d:%02d", (now+d)/1440 != now/1440 ? "翌日 " : "",m/60,m%60); return text;
        }
    }
    return "";
}
void updateAutomation() {
    if (!settings.enabled) return;
    auto now = minuteNow();
    if (now < 0) return;
    int m = now%1440;
    bool enabling = immediate;
    if (due(now,highWater,immediate,event(settings,m,saved[0],saved[1],saved[2]))) {
        pending[0] = pending[1] = true;
        outcomes[0] = outcomes[1] = "待機";
        capture[0] = !enabling && m == settings.bed-15;
        capture[1] = !enabling && m == settings.bed%1440;
        deadline = now; // At most the remainder of this minute; never replay stale events.
        status = "自動制御を実行中";
    }
    immediate = false;
    auto p = phase(settings,m);
    for (int n=0;n<2;++n) {
        if (!pending[n]) continue;
        if (now != deadline) { pending[n]=false; outcomes[n]="見送り"; continue; }
        auto id=static_cast<light::Id>(n);
        if (isLightingBusy(id)) continue;
        if (capture[n]) {
            capture[n]=false;
            if (!saveNight(n)) { pending[n]=false; outcomes[n]="保存失敗"; continue; }
        }
        int b=-1,c=-1; bool full=false, restore=false;
        light::Power power=light::Power::On;
        switch(p) {
            case Phase::Off: power=light::Power::Off; break;
            case Phase::Night: power=light::Power::Night; break;
            case Phase::Day: b=n==0?4:5; c=5; full=n==1; break;
            case Phase::Evening: b=n==0?2:3; c=2; break;
            case Phase::Late:
                if (n==0 && kitchenBedtimeNight(settings,m)) power=light::Power::Night;
                else b=bedtimeLevel(settings,m,id);
                c=0; break;
            case Phase::Dawn:
                b=ramp(saved[n==0?0:1],n==0?4:5,m-settings.wake);
                c=ramp(saved[2],5,m-settings.wake); restore=n==1 && m==settings.wake; break;
        }
        bool ok=requestAutomaticLighting(id,power,b,c,full,restore && livingNightKnown,saved[1],saved[2]);
        pending[n]=false;
        outcomes[n] = ok ? (isLightingBusy(id) ? "送信中" : "完了") : "実行不可";
        Serial.printf("AUTO_COMMAND: device=%d minute=%d accepted=%d\n",n,m,ok);
        status = ok ? "受付済み：結果は各照明タブで確認" : String(n==0?"キッチン":"リビング")+"：実行不可（照明タブを確認）";
    }
    static int64_t drawn=-1;
    static String lastStatus;
    String currentStatus=automationStatus();
    if (now!=drawn || enabling || currentStatus!=lastStatus) {
        drawn=now; lastStatus=currentStatus; redrawLighting();
    }
}
