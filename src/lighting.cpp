#include "lighting.h"
#include "light_model.h"
#include "remo_ca.h"
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
using Job = Plan;
struct Result { Job job; int completed; int code; };
QueueHandle_t requests = nullptr;
QueueHandle_t results = nullptr;
State estimated;
bool busy = false;
std::atomic<int> progress{0};
int total = 0;
String notice = "点灯させてから下の合わせボタンを押す";
uint32_t sentAt[30]{};
unsigned sentCount = 0;
uint32_t lastSent = 0;
bool hasSent = false;
uint32_t rateLimitedAt = 0;
bool rateLimited = false;

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
bool configured() {
    return REMO_ACCESS_TOKEN[0] && validUrl(REMO_LIGHT_SWITCH) &&
           validUrl(REMO_LIGHT_UP) && validUrl(REMO_LIGHT_DOWN);
}

int sendSignal(Signal signal) {
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
    const char* url = signal == Signal::Cycle ? REMO_LIGHT_SWITCH :
                      signal == Signal::Up ? REMO_LIGHT_UP : REMO_LIGHT_DOWN;
    if (!http.begin(client, url)) return -102;
    http.addHeader("Authorization", String("Bearer ") + REMO_ACCESS_TOKEN);
    http.addHeader("accept", "application/json");
    sentAt[sentCount % 30] = millis();
    ++sentCount;
    const int code = http.POST("");
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
            // Keep at least one second between signals, including separate jobs.
            while (hasSent && millis() - lastSent < 1000) vTaskDelay(pdMS_TO_TICKS(20));
            result.code = sendSignal(job.signals[i]);
            if (result.code < 200 || result.code >= 300) break;
            ++result.completed;
            progress.store(result.completed);
        }
        xQueueSend(results, &result, portMAX_DELAY);
    }
}

void button(int column, int row, const char* label, uint32_t color = TFT_DARKGREY) {
    auto& d = M5.Display;
    const int width = row == 2 ? 308 : 100;
    const int x = 6 + column * (width + 4);
    const int y = 84 + row * 44;
    d.fillRoundRect(x, y, width, 39, 5, busy ? TFT_DARKGREY : color);
    d.setTextColor(TFT_WHITE, busy ? TFT_DARKGREY : color);
    d.drawString(label, x + width / 2, y + 19);
}

bool enqueue(const Job& job) {
    if (busy) { notice = "送信中です。完了後に再操作してください"; return false; }
    if (!requests || !results) { notice = "送信機能を開始できません"; return false; }
    if (!configured()) { notice = "remo_secrets.h を設定してください"; return false; }
    if (WiFi.status() != WL_CONNECTED) { notice = "Wi-Fi未接続です"; return false; }
    if (time(nullptr) < 1704067200) { notice = "時刻同期中です。少し待って再操作"; return false; }
    if (xQueueSend(requests, &job, 0) != pdTRUE) { notice = "送信待ちです"; return false; }
    busy = true;
    total = job.count;
    progress.store(0);
    notice = "送信中：照明にRemoを向けてください";
    return true;
}

void start(Signal signal, int count = 1, int syncLevel = -1) {
    Job job;
    job.count = count;
    job.syncLevel = syncLevel;
    for (int i = 0; i < count; ++i) job.signals[i] = signal;
    enqueue(job);
}

void target(Power desired) {
    const int count = pressesTo(estimated.power, desired);
    if (count < 0) notice = "点灯させてから下の合わせボタンを押す";
    else if (count == 0) notice = "推定では既にその状態です";
    else start(Signal::Cycle, count);
}

void handleButton(int column, int row) {
    if (row == 0) target(column == 0 ? Power::On : column == 1 ? Power::Night : Power::Off);
    else if (row == 1 && column == 2) start(Signal::Cycle);
    else if (row == 2) {
        // The user confirms that the fixture is currently on by pressing this.
        start(Signal::Up, kSyncPresses, kMaxStep);
        if (busy) {
            estimated.power = Power::On;
            estimated.brightness = -1;
        }
    } else if (row == 1) {
        if (estimated.power != Power::On) notice = "調光は点灯状態に合わせてから操作";
        else start(column == 0 ? Signal::Down : Signal::Up);
    }
}

}  // namespace

void redrawLighting() {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::efontJA_16);
    d.setTextSize(1);
    d.setTextDatum(middle_center);
    d.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    String wifi = WiFi.status() == WL_CONNECTED
        ? "Wi-Fi接続済 " + WiFi.localIP().toString() + " " + String(WiFi.RSSI()) + "dBm"
        : "Wi-Fi未接続 / 接続待ち";
    d.drawString(wifi, 160, 12);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawString(String("照明（推定）：") + powerName(estimated.power), 160, 37);
    String level = estimated.brightness < 0 ? "未確認" : String((estimated.brightness + 1) * 20);
    d.drawString("明るさ（推定）：" + level, 160, 60);
    button(0, 0, "点灯"); button(1, 0, "常夜灯"); button(2, 0, "消灯");
    button(0, 1, "暗く"); button(1, 1, "明るく"); button(2, 1, "電源1回");
    button(0, 2, "点灯中に合わせる（最大へ）", TFT_BLUE);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setFont(&fonts::efontJA_12);
    d.drawString(busy ? String("送信中 ") + progress.load() + "/" + total : notice, 160, 225);
}

void beginLighting() {
    requests = xQueueCreate(1, sizeof(Job));
    results = xQueueCreate(1, sizeof(Result));
    if (!requests || !results ||
        xTaskCreate(senderTask, "remo", 12288, nullptr, 1, nullptr) != pdPASS) {
        requests = nullptr;
        notice = "送信機能を開始できません";
    }
    redrawLighting();
    Serial.printf("LIGHTING_READY: configured=%d\n", configured());
}

void updateLighting() {
    Result result;
    if (results && xQueueReceive(results, &result, 0) == pdTRUE) {
        for (int i = 0; i < result.completed; ++i) apply(estimated, result.job.signals[i]);
        if (result.completed == result.job.count) {
            if (result.job.syncLevel >= 0) estimated.brightness = result.job.syncLevel;
            notice = "送信完了。実際と違う場合は合わせ直し";
        } else {
            uncertain(estimated, result.job.signals[result.completed]);
            notice = result.code == 429 ? "送信制限：5分待ってから再操作" :
                     "送信失敗 " + String(result.code) + "：状態を合わせ直してください";
        }
        Serial.printf("REMO_RESULT: completed=%d/%d code=%d\n", result.completed, result.job.count, result.code);
        busy = false;
        redrawLighting();
    }
    static int lastProgress = -1;
    if (busy && progress.load() != lastProgress) {
        lastProgress = progress.load();
        redrawLighting();
    }
    const auto touch = M5.Touch.getDetail();
    if (!busy && touch.wasPressed() && touch.y >= 84 && touch.y < 211 && touch.x >= 6) {
        const int row = (touch.y - 84) / 44;
        const int width = row == 2 ? 308 : 100;
        const int column = (touch.x - 6) / (width + 4);
        if (column < (row == 2 ? 1 : 3) && (touch.y - 84) % 44 < 39 &&
            (touch.x - 6) % (width + 4) < width) {
            handleButton(column, row);
            redrawLighting();
        }
    }
}

light::State getLightingState() {
    return estimated;
}

bool isLightingBusy() {
    return busy;
}

bool requestLighting(light::Power power, int brightness) {
    const auto plan = light::planTarget(estimated, power, brightness);
    bool accepted = false;
    if (busy) notice = "送信中です。完了後に再操作してください";
    else if (!plan.valid) notice = "点灯させてから下の合わせボタンを押す";
    else if (plan.count == 0) {
        notice = "推定では既にその状態です";
        accepted = true;
    } else accepted = enqueue(plan);
    redrawLighting();
    return accepted;
}
