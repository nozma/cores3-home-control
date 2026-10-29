#include "lighting.h"
#include "error_log.h"
#include "light_storage.h"
#include "light_model.h"
#include "remo_ca.h"
#if __has_include("remo_local.h")
#include "remo_local.h"
#else
#include "remo_local.example.h"
#endif
#if __has_include("living_secrets.h")
#include "living_secrets.h"
#else
#include "living_secrets.example.h"
#endif
#if __has_include("remo_secrets.h")
#include "remo_secrets.h"
#else
#include "remo_secrets.example.h"
#endif

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <ctime>

namespace {
using namespace light;
struct Job : Plan { Id id = Id::Kitchen; };
struct Result { Job job; int completed; int code; LightingError error; };
QueueHandle_t requests = nullptr;
QueueHandle_t results = nullptr;
State states[2];
bool busy[2]{};
bool normal[2]{};
std::atomic<int> progress[2]{};
int total[2]{};
String notices[2] = {"点灯中にキャリブレーションを押す", "照明を選んで操作してください"};
int selected = 0;
M5Canvas canvas(&M5.Display);
bool canvasReady = false;

lgfx::LGFXBase& drawingSurface() {
    if (canvasReady) return canvas;
    return M5.Display;
}

int index(Id id) { return static_cast<int>(id); }
Id selectedId() { return static_cast<Id>(selected); }
uint32_t sentAt[30]{};
unsigned sentCount = 0;
uint32_t lastSent = 0;
bool hasSent = false;
uint32_t rateLimitedAt = 0;
bool rateLimited = false;
uint32_t localUntil = 0;
bool localPending = false;

const char* localPayload(Signal signal) {
    return signal == Signal::Up ? REMO_LOCAL_UP : signal == Signal::Down ? REMO_LOCAL_DOWN :
           signal == Signal::Warm ? REMO_LOCAL_WARM : REMO_LOCAL_COOL;
}
unsigned localDuration(Signal signal) {
    return signal == Signal::Up ? REMO_LOCAL_UP_MS : signal == Signal::Down ? REMO_LOCAL_DOWN_MS :
           signal == Signal::Warm ? REMO_LOCAL_WARM_MS : REMO_LOCAL_COOL_MS;
}

int sendLocal(Signal signal) {
    if (WiFi.status() != WL_CONNECTED) return -100;
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(800);
    // Remo may respond only after emitting the burst; allow for LAN latency too.
    http.setTimeout(localDuration(signal) + 5000);
    http.setReuse(false);
    if (!http.begin(client, String("http://") + REMO_LOCAL_HOST + "/messages")) return -102;
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Content-Type", "application/json");
    const uint32_t started = millis();
    int code = http.POST(String(localPayload(signal)));
    Serial.printf("REMO_LOCAL_RESULT: signal=%d elapsed_ms=%lu code=%d\n",
                  static_cast<int>(signal), static_cast<unsigned long>(millis() - started), code);
    http.end();
    // Even an uncertain HTTP result may have started IR transmission.
    localUntil = millis() + localDuration(signal) + 80;
    localPending = true;
    return code;
}

const char* powerName(Power power) {
    switch (power) {
        case Power::On: return "点灯";
        case Power::Night: return "常夜灯";
        case Power::Off: return "消灯";
        default: return "未確認";
    }
}
bool validUrl(const char* value) {
    String url(value);
    return url.startsWith("https://api.nature.global/1/signals/") && url.endsWith("/send");
}
bool configured(Id id) {
    if (id == Id::Living) return REMO_ACCESS_TOKEN[0] && REMO_LIVING_ID[0] &&
        validUrl(REMO_LIVING_FULL) && validUrl(REMO_LIVING_NORMAL);
    return REMO_ACCESS_TOKEN[0] && validUrl(REMO_LIGHT_SWITCH) &&
           validUrl(REMO_LIGHT_UP) && validUrl(REMO_LIGHT_DOWN);
}

int sendSignal(Id id, Signal signal) {
    if (WiFi.status() != WL_CONNECTED) return -100;
    if (time(nullptr) < 1704067200) return -101;
    const uint32_t now = millis();
    if (rateLimited && now - rateLimitedAt < 300000) return 429;
    if (sentCount >= 30 && now - sentAt[sentCount % 30] < 300000) return 429;

    WiFiClientSecure client;
    client.setCACert(kRemoRootCa);
    client.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    String url;
    String payload;
    if (id == Id::Kitchen) {
        url = signal == Signal::Cycle ? REMO_LIGHT_SWITCH :
              signal == Signal::Up ? REMO_LIGHT_UP : REMO_LIGHT_DOWN;
    } else if (signal == Signal::Full || signal == Signal::Normal || signal == Signal::On) {
        url = signal == Signal::Full ? REMO_LIVING_FULL : REMO_LIVING_NORMAL;
    } else {
        url = String("https://api.nature.global/1/appliances/") + REMO_LIVING_ID + "/light";
        const char* button = signal == Signal::On ? "on" : signal == Signal::Off ? "off" :
            signal == Signal::Night ? "night" : signal == Signal::Up ? "bright-up" :
            signal == Signal::Down ? "bright-down" : signal == Signal::Warm ? "colortemp-down" : "colortemp-up";
        payload = String("button=") + button;
    }
    if (!http.begin(client, url)) return -102;
    http.addHeader("Authorization", String("Bearer ") + REMO_ACCESS_TOKEN);
    http.addHeader("accept", "application/json");
    sentAt[sentCount % 30] = millis();
    ++sentCount;
    if (payload.length()) http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const uint32_t started = millis();
    const int code = http.POST(payload);
    Serial.printf("REMO_CLOUD_RESULT: device=%d signal=%d elapsed_ms=%lu code=%d\n",
                  index(id), static_cast<int>(signal), static_cast<unsigned long>(millis() - started), code);
    http.end();
    lastSent = millis();
    hasSent = true;
    if (code == 429) {
        rateLimited = true;
        rateLimitedAt = millis();
    }
    return code;
}

void senderTask(void*) {
    Job job;
    uint32_t previousEnd = 0;
    bool attempted = false;
    while (true) {
        if (xQueueReceive(requests, &job, portMAX_DELAY) != pdTRUE) continue;
        Result result{job, 0, 200, {}};
        for (int i = 0; i < job.count; ++i) {
            // All send paths share this worker so emitted IR never overlaps.
            while (localPending && static_cast<int32_t>(localUntil - millis()) > 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            localPending = false;
            const bool local = job.id == Id::Living && isAdjustment(job.signals[i]);
            if (!local)
                while (hasSent && millis() - lastSent < 1000) vTaskDelay(pdMS_TO_TICKS(20));
            const uint32_t started = millis();
            const time_t now = time(nullptr);
            result.error.unixTime = now >= 1704067200 ? now : 0;
            result.error.uptimeMs = started;
            result.error.gapMs = attempted ? static_cast<int32_t>(started - previousEnd) : -1;
            result.error.rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
            result.error.device = index(job.id);
            result.error.signal = static_cast<uint8_t>(job.signals[i]);
            result.error.local = local;
            result.error.completed = i;
            result.error.total = job.count;
            if (local) {
                result.code = sendLocal(job.signals[i]);
            } else {
                result.code = sendSignal(job.id, job.signals[i]);
            }
            previousEnd = millis();
            attempted = true;
            result.error.elapsedMs = previousEnd - started;
            result.error.code = result.code;
            if (result.code < 200 || result.code >= 300) break;
            ++result.completed;
            progress[index(job.id)].store(result.completed);
        }
        xQueueSend(results, &result, portMAX_DELAY);
    }
}

void button(int x, int y, int width, const char* label, uint32_t color = TFT_DARKGREY) {
    auto& d = drawingSurface();
    uint32_t background = busy[selected] ? TFT_DARKGREY : color;
    d.fillRoundRect(x, y, width, 31, 4, background);
    d.setTextColor(TFT_WHITE, background);
    d.drawString(label, x + width / 2, y + 15);
}

bool enqueue(const Job& job) {
    int n = index(job.id);
    auto& notice = notices[n];
    if (busy[n]) { notice = "送信中です。完了後に再操作してください"; return false; }
    if (!requests || !results) { notice = "送信機能を開始できません"; return false; }
    bool needsCloud = false;
    for (int i = 0; i < job.count; ++i) {
        bool local = job.id == Id::Living && isAdjustment(job.signals[i]);
        if (local && (!REMO_LOCAL_HOST[0] || !localPayload(job.signals[i])[0])) {
            notice = "ローカルIR信号の設定を確認してください"; return false;
        }
        if (!local) needsCloud = true;
    }
    if (needsCloud && !configured(job.id)) { notice = "Remoの設定を確認してください"; return false; }
    if (WiFi.status() != WL_CONNECTED) { notice = "Wi-Fi未接続です"; return false; }
    if (needsCloud && time(nullptr) < 1704067200) { notice = "時刻同期中です。少し待って再操作"; return false; }
    if (!prepareLightState(job.id, states[n], normal[n], job)) {
        notice = "状態を保存できません。送信を中止しました"; return false;
    }
    if (xQueueSend(requests, &job, 0) != pdTRUE) {
        saveLightState(job.id, states[n], normal[n]);
        notice = "送信待ちです"; return false;
    }
    busy[n] = true;
    total[n] = job.count;
    progress[n].store(0);
    notice = "送信中";
    return true;
}

void start(Signal signal, int count = 1, int syncLevel = -1) {
    Job job;
    job.id = selectedId();
    job.count = count;
    job.syncLevel = syncLevel;
    for (int i = 0; i < count; ++i) job.signals[i] = signal;
    enqueue(job);
}

void handleButton(int column, int row) {
    auto& estimated = states[selected];
    auto& notice = notices[selected];
    if (row == 0) requestLighting(column == 0 ? Power::On : column == 1 ? Power::Night : Power::Off,
                                  -1, selectedId());
    else if (selected == 0) {
        if (row == 1 && column == 2) start(Signal::Cycle);
        else if (row == 2) {
            start(Signal::Up, kSyncPresses, kMaxStep);
            if (busy[0]) { estimated.power = Power::On; estimated.brightness = -1; }
        } else if (row == 1) {
            if (estimated.power != Power::On) notice = "点灯状態に合わせてから調光してください";
            else start(column == 0 ? Signal::Down : Signal::Up);
        }
    }
}


}  // namespace

void redrawLighting() {
    auto& d = drawingSurface();
    constexpr uint32_t bg = 0x111e1c, surface = 0x233630, accent = 0xc8dda7;
    constexpr uint32_t text = 0xe9f0e9, muted = 0x95aba0, selectedBg = 0x30483e;
    d.fillScreen(bg);
    d.setTextSize(1);
    d.setFont(&fonts::efontJA_12);
    d.setTextDatum(middle_center);
    auto label = [&](const String& value, int x, int y, uint32_t color = 0xe9f0e9) {
        d.setTextColor(color, bg);
        d.drawString(value, x, y);
    };
    auto control = [&](int x, int y, int w, int h, const String& value,
                       bool active = false, bool secondary = false) {
        uint32_t fill = secondary ? 0x1b2b26 : active ? accent : surface;
        d.fillRoundRect(x, y, w, h, 5, fill);
        d.setTextColor(busy[selected] ? muted : active ? bg : secondary ? 0xa7baac : text, fill);
        d.drawString(value, x + w / 2, y + h / 2);
    };
    for (int n = 0; n < 2; ++n) {
        d.fillRoundRect(8 + n * 76, 8, 72, 26, 6, selected == n ? selectedBg : bg);
        d.setTextColor(selected == n ? text : muted, selected == n ? selectedBg : bg);
        d.drawString(n == 0 ? "キッチン" : "リビング", 44 + n * 76, 21);
    }
    bool connected = WiFi.status() == WL_CONNECTED;
    const uint32_t wifiColor = connected ? uint32_t{0xb9d58d} : uint32_t{0x819087};
    d.drawArc(298, 28, 13, 11, 225, 315, wifiColor);
    d.drawArc(298, 28, 8, 6, 225, 315, wifiColor);
    d.fillCircle(298, 27, 2, wifiColor);
    if (!connected) {
        // Clear a gap behind the slash so the disconnected state stays legible.
        d.drawLine(287, 13, 307, 30, bg);
        d.drawLine(287, 14, 307, 31, bg);
        d.drawLine(287, 12, 307, 29, wifiColor);
        d.drawLine(287, 13, 307, 30, wifiColor);
    }
    const auto& state = states[selected];
    d.setFont(&fonts::efontJA_16);
    d.setTextDatum(middle_left);
    label(state.power == Power::On ? "点灯中" : powerName(state.power), 10, 49);
    d.setTextDatum(middle_center);
    d.setFont(&fonts::efontJA_12);
    for (int i = 0; i < 3; ++i) {
        Power power = i == 0 ? Power::On : i == 1 ? Power::Night : Power::Off;
        control(8 + i * 103, 64, 98, 28,
                i == 0 ? "点灯" : i == 1 ? "常夜灯" : "消灯", state.power == power);
    }
    auto axisLabel = [&](const char* name, const String& value, int y) {
        d.setTextDatum(middle_left); label(name, 10, y, muted);
        d.setTextDatum(middle_right); label(value, 310, y);
        d.setTextDatum(middle_center);
    };
    if (selected == 0) {
        String value = state.brightness < 0 ? "未確認" : String((state.brightness + 1) * 20) + "%";
        axisLabel("明るさ", value, 110);
        for (int i = 0; i < 5; ++i) {
            control(8 + i * 61, 122, 57, 25, String((i + 1) * 20) + "%", state.brightness == i);
        }
        control(10, 185, 116, 25, "キャリブレーション", false, true);
        label("点灯中に押す", 165, 197, muted);
        control(240, 185, 70, 25, "電源ボタン", false, true);
    } else {
        axisLabel("明るさ", state.brightness < 0 ? (normal[1] ? "普段設定" : "未確認") : String(state.brightness * 20) + "%", 104);
        axisLabel("色", state.color < 0 ? (normal[1] ? "普段設定" : "未確認") : "", 151);
        constexpr uint32_t colors[] = {0xffbd6b, 0xffd08c, 0xffdfb0, 0xffeacb, 0xfff2e3, 0xf4f8ff};
        for (int i = 0; i < 6; ++i) {
            int x = 8 + i * 51;
            control(x, 114, 47, 25, String(i * 20) + "%", state.brightness == i);
            d.fillRoundRect(x, 161, 47, 25, 4, colors[i]);
            if (state.color == i) {
                d.drawRoundRect(x, 161, 47, 25, 4, accent);
                d.drawRoundRect(x + 1, 162, 45, 23, 3, bg);
                d.drawLine(x + 18, 173, x + 22, 177, bg);
                d.drawLine(x + 22, 177, x + 29, 169, bg);
            }
        }
        control(8, 193, 149, 25, "全灯");
        control(162, 193, 150, 25, "普段");
    }
    // Explicit RGB888: an int literal selects the RGB565 color overload.
    d.drawFastHLine(10, 219, 300, uint32_t{0x25302c});
    d.setTextDatum(middle_left);
    String notice = busy[selected] ? String("送信中 ") + progress[selected].load() + " / " + total[selected] : notices[selected];
    // Keep long errors inside the footer without changing their stored detail.
    while (d.textWidth(notice) > 298 && notice.length() > 0) {
        int pos = notice.length() - 1;
        while (pos > 0 && (static_cast<unsigned char>(notice[pos]) & 0xc0) == 0x80) --pos;
        notice.remove(pos);
    }
    label(notice, 10, 230, accent);
    d.setTextDatum(middle_center);
    if (canvasReady) canvas.pushSprite(0, 0);
}

void beginLighting() {
    beginLightStorage();
    for (int n = 0; n < 2; ++n) {
        if (loadLightState(static_cast<Id>(n), states[n], normal[n]))
            notices[n] = "保存した推定状態を復元しました";
    }
    canvas.setColorDepth(16);
    canvasReady = canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
    requests = xQueueCreate(2, sizeof(Job));
    results = xQueueCreate(2, sizeof(Result));
    if (!requests || !results ||
        xTaskCreate(senderTask, "remo", 12288, nullptr, 1, nullptr) != pdPASS) {
        requests = nullptr;
        notices[0] = notices[1] = "送信機能を開始できません";
    }
    redrawLighting();
    Serial.printf("LIGHTING_READY: kitchen=%d living=%d\n", configured(Id::Kitchen), configured(Id::Living));
    Serial.printf("LOCAL_IR_READY: %d\n", REMO_LOCAL_HOST[0] && REMO_LOCAL_UP[0] && REMO_LOCAL_DOWN[0] && REMO_LOCAL_WARM[0] && REMO_LOCAL_COOL[0]);
}

void updateLighting() {
    Result result;
    if (results && xQueueReceive(results, &result, 0) == pdTRUE) {
        int n = index(result.job.id);
        auto& estimated = states[n];
        for (int i = 0; i < result.completed; ++i) {
            if (n == 0) apply(estimated, result.job.signals[i]);
            else light::applyLiving(estimated, result.job.signals[i]);
        }
        if (result.completed == result.job.count) {
            if (result.job.syncLevel >= 0) estimated.brightness = result.job.syncLevel;
            if (result.job.syncColor >= 0) estimated.color = result.job.syncColor;
            if (n == 1 && result.completed > 0) normal[1] = result.job.signals[0] == Signal::On || result.job.signals[0] == Signal::Normal;
            notices[n] = "送信完了";
        } else {
            saveLightingError(result.error);
            Signal failed = result.job.signals[result.completed];
            if (n == 0) uncertain(estimated, failed);
            else {
                uncertainLiving(estimated, failed);
                normal[1] = false;
            }
            notices[n] = result.code == HTTPC_ERROR_READ_TIMEOUT ? "応答待ち時間切れ：実際の状態を確認" :
                result.code == 429 ? "送信制限：5分待ってから再操作" :
                "送信失敗 " + String(result.code) + "：実際の状態を確認してください";
        }
        if (!saveLightState(result.job.id, estimated, normal[n]))
            notices[n] = "状態保存失敗：再起動後は一部未確認";
        Serial.printf("REMO_RESULT: device=%d completed=%d/%d code=%d\n", n, result.completed, result.job.count, result.code);
        busy[n] = false;
        redrawLighting();
    }
    static int lastProgress[2] = {-1, -1};
    if (busy[selected] && progress[selected].load() != lastProgress[selected]) {
        lastProgress[selected] = progress[selected].load();
        redrawLighting();
    }
    const auto touch = M5.Touch.getDetail();
    if (!touch.wasPressed()) return;
    // CoreS3 SE's touch panel extends 40 pixels below the visible LCD.
    if (touch.y >= 240 && touch.y < 280 && touch.x >= 0 && touch.x < 320) {
        selected = touch.x < 160 ? 0 : 1;
        redrawLighting();
        return;
    }
    if (touch.y >= 8 && touch.y < 34 && touch.x >= 8 && touch.x < 156) {
        selected = touch.x < 84 ? 0 : 1;
        redrawLighting();
        return;
    }
    if (busy[selected]) return;
    auto inside = [&](int x, int y, int w, int h) {
        return touch.x >= x && touch.x < x + w && touch.y >= y && touch.y < y + h;
    };
    for (int i = 0; i < 3; ++i) {
        if (inside(8 + i * 103, 64, 98, 28)) {
            requestLighting(i == 0 ? Power::On : i == 1 ? Power::Night : Power::Off, -1, selectedId());
        }
    }
    if (selected == 1) {
        for (int i = 0; i < 6; ++i) {
            if (inside(8 + i * 51, 114, 47, 25)) requestLivingPercent(Axis::Brightness, i);
            if (inside(8 + i * 51, 161, 47, 25)) requestLivingPercent(Axis::Color, i);
        }
        if (inside(8, 193, 149, 25)) start(Signal::Full);
        if (inside(162, 193, 150, 25)) start(Signal::Normal);
    } else {
        for (int i = 0; i < 5; ++i) {
            if (!inside(8 + i * 61, 122, 57, 25)) continue;
            if (states[0].power != Power::On) notices[0] = "点灯状態に合わせてから調光してください";
            else requestLighting(Power::On, i, Id::Kitchen);
        }
        if (inside(10, 185, 116, 25)) handleButton(0, 2);
        if (inside(240, 185, 70, 25)) handleButton(2, 1);
    }
    redrawLighting();
}

bool requestLivingPercent(Axis axis, int step) {
    Plan plan = planLivingAxis(states[1], axis, step);
    bool accepted = false;
    if (busy[1]) notices[1] = "送信中です。完了後に再操作してください";
    else if (!plan.valid) notices[1] = "指定は0〜100%の20%刻みです";
    else if (plan.count == 0) { notices[1] = "推定では既にその値です"; accepted = true; }
    else {
        Job job;
        static_cast<Plan&>(job) = plan;
        job.id = Id::Living;
        accepted = enqueue(job);
    }
    redrawLighting();
    return accepted;
}

light::State getLightingState(Id id) { return states[index(id)]; }
bool isLightingBusy(Id id) { return busy[index(id)]; }

bool requestLighting(Power power, int brightness, Id id) {
    int n = index(id);
    Plan plan = n == 0 ? light::planTarget(states[n], power, brightness) :
                        light::planLiving(states[n], power, brightness);
    bool accepted = false;
    if (busy[n]) notices[n] = "送信中です。完了後に再操作してください";
    else if (!plan.valid) notices[n] = n == 0 ? "点灯中にキャリブレーションを押す" : "明るさの段階数を設定してください";
    else if (plan.count == 0) { notices[n] = "推定では既にその状態です"; accepted = true; }
    else {
        Job job;
        static_cast<Plan&>(job) = plan;
        job.id = id;
        accepted = enqueue(job);
    }
    redrawLighting();
    return accepted;
}
