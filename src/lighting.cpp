#include "lighting.h"
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
struct Result { Job job; int completed; int code; };
QueueHandle_t requests = nullptr;
QueueHandle_t results = nullptr;
State states[2];
bool busy[2]{};
bool normal[2]{};
std::atomic<int> progress[2]{};
int total[2]{};
String notices[2] = {"点灯させてから合わせボタンを押す", "照明を選んで操作してください"};
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
    http.setTimeout(1200);
    http.setReuse(false);
    if (!http.begin(client, String("http://") + REMO_LOCAL_HOST + "/messages")) return -102;
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(String(localPayload(signal)));
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
    const int code = http.POST(payload);
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
    while (true) {
        if (xQueueReceive(requests, &job, portMAX_DELAY) != pdTRUE) continue;
        Result result{job, 0, 200};
        for (int i = 0; i < job.count; ++i) {
            // All send paths share this worker so emitted IR never overlaps.
            while (localPending && static_cast<int32_t>(localUntil - millis()) > 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            localPending = false;
            if (job.id == Id::Living && isAdjustment(job.signals[i])) {
                result.code = sendLocal(job.signals[i]);
            } else {
                while (hasSent && millis() - lastSent < 1000) vTaskDelay(pdMS_TO_TICKS(20));
                result.code = sendSignal(job.id, job.signals[i]);
            }
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
    if (xQueueSend(requests, &job, 0) != pdTRUE) { notice = "送信待ちです"; return false; }
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
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::efontJA_16);
    d.setTextSize(1);
    d.setTextDatum(middle_center);
    for (int n = 0; n < 2; ++n) {
        uint32_t bg = selected == n ? TFT_BLUE : TFT_DARKGREY;
        d.fillRoundRect(4 + n * 118, 3, 114, 28, 4, bg);
        d.setTextColor(TFT_WHITE, bg);
        d.drawString(n == 0 ? "キッチン" : "リビング", 61 + n * 118, 17);
    }
    d.setFont(&fonts::efontJA_12);
    d.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    d.drawString(WiFi.status() == WL_CONNECTED ? "Wi-Fi接続" : "Wi-Fi待ち", 278, 17);
    const auto& estimated = states[selected];
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    if (selected == 0) {
        String level = estimated.brightness < 0 ? "未確認" : String((estimated.brightness + 1) * 20) + "%";
        d.setFont(&fonts::efontJA_16);
        d.drawString(String(powerName(estimated.power)) + "（推定）  明るさ：" + level, 160, 48);
        button(4, 68, 101, "点灯"); button(109, 68, 101, "常夜灯"); button(214, 68, 101, "消灯");
        button(4, 104, 101, "暗く"); button(109, 104, 101, "明るく"); button(214, 104, 101, "電源1回");
        button(4, 140, 311, "点灯中に合わせる（最大へ）", TFT_BLUE);
    } else {
        d.setFont(&fonts::efontJA_16);
        button(4, 35, 101, "点灯"); button(109, 35, 101, "常夜灯"); button(214, 35, 101, "消灯");
        d.setFont(&fonts::efontJA_12);
        d.setTextColor(TFT_WHITE, TFT_BLACK);
        String brightness = estimated.brightness < 0 ? (normal[1] ? "普段設定" : "未確認") : String(estimated.brightness * 20) + "%";
        String color = estimated.color < 0 ? (normal[1] ? "普段設定" : "未確認") : String(estimated.color * 20) + "%";
        d.drawString(String(powerName(estimated.power)) + "（推定） 明るさ：" + brightness, 160, 77);
        d.drawString("色（暖0% → 白100%）：" + color, 160, 131);
        for (int i = 0; i < 6; ++i) {
            String label = String(i * 20) + "%";
            uint32_t brightColor = estimated.brightness == i ? TFT_BLUE : TFT_DARKGREY;
            button(4 + i * 52, 86, 48, label.c_str(), brightColor);
            uint32_t colorColor = estimated.color == i ? TFT_BLUE : TFT_DARKGREY;
            button(4 + i * 52, 142, 48, label.c_str(), colorColor);
        }
        d.setFont(&fonts::efontJA_16);
        button(4, 181, 153, "全灯", TFT_BLUE); button(162, 181, 153, "普段", TFT_BLUE);
    }
    d.setFont(&fonts::efontJA_12);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.drawString(busy[selected] ? String("送信中 ") + progress[selected].load() + "/" + total[selected] : notices[selected], 160, 225);
    // Present the completed frame without exposing the clear/draw steps.
    if (canvasReady) canvas.pushSprite(0, 0);
}

void beginLighting() {
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
            notices[n] = "送信完了（状態は推定です）";
        } else {
            Signal failed = result.job.signals[result.completed];
            if (n == 0) uncertain(estimated, failed);
            else {
                uncertainLiving(estimated, failed);
                normal[1] = false;
            }
            notices[n] = result.code == 429 ? "送信制限：5分待ってから再操作" :
                "送信失敗 " + String(result.code) + "：実際の状態を確認してください";
        }
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
    if (touch.y >= 3 && touch.y < 31 && touch.x >= 4 && touch.x < 236) {
        selected = touch.x < 122 ? 0 : 1;
        redrawLighting();
        return;
    }
    if (busy[selected] || touch.x < 4 || touch.x >= 315) return;
    if (selected == 1) {
        if (touch.y >= 35 && touch.y < 66) {
            int column = (touch.x - 4) / 105;
            if ((touch.x - 4) % 105 < 101)
                requestLighting(column == 0 ? Power::On : column == 1 ? Power::Night : Power::Off, -1, Id::Living);
        } else if ((touch.y >= 86 && touch.y < 117) || (touch.y >= 142 && touch.y < 173)) {
            int step = (touch.x - 4) / 52;
            if (step < 6 && (touch.x - 4) % 52 < 48)
                requestLivingPercent(touch.y < 117 ? Axis::Brightness : Axis::Color, step);
        } else if (touch.y >= 181 && touch.y < 212) {
            start(touch.x < 158 ? Signal::Full : Signal::Normal);
        }
    } else if (touch.y >= 68 && touch.y < 171) {
        int row = (touch.y - 68) / 36;
        if ((touch.y - 68) % 36 >= 31) return;
        int column = row < 2 ? (touch.x - 4) / 105 : 0;
        handleButton(column, row);
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
    else if (!plan.valid) notices[n] = n == 0 ? "点灯させてから合わせボタンを押す" : "明るさの段階数を設定してください";
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
