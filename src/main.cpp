#include <M5Unified.h>
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kRetryDelayMs = 5000;
enum class ConnectionState { Unconfigured, Connecting, Connected, Disconnected };
ConnectionState state = ConnectionState::Unconfigured;
uint32_t stateSince = 0;
uint32_t lastRefresh = 0;

void drawStatus() {
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(3);
    display.drawString("Home Control", display.width() / 2, 35);
    display.setTextSize(2);

    const char* label = "Wi-Fi: Not configured";
    uint32_t color = TFT_YELLOW;
    if (state == ConnectionState::Connecting) {
        label = "Wi-Fi: Connecting...";
    } else if (state == ConnectionState::Connected) {
        label = "Wi-Fi: Connected";
        color = TFT_GREEN;
    } else if (state == ConnectionState::Disconnected) {
        label = "Wi-Fi: Disconnected";
        color = TFT_RED;
    }
    display.setTextColor(color, TFT_BLACK);
    display.drawString(label, display.width() / 2, 100);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    if (state == ConnectionState::Connected) {
        display.drawString("IP: " + WiFi.localIP().toString(), display.width() / 2, 145);
        display.drawString("Signal: " + String(WiFi.RSSI()) + " dBm", display.width() / 2, 180);
    } else if (state == ConnectionState::Unconfigured) {
        display.drawString("Set include/secrets.h", display.width() / 2, 155);
    } else if (state == ConnectionState::Disconnected) {
        display.drawString("Retrying in 5 sec", display.width() / 2, 155);
    }
}

void setState(ConnectionState next) {
    state = next;
    stateSince = millis();
    // Do not log SSIDs or passwords.
    Serial.printf("WIFI_STATE: %s\n",
                  next == ConnectionState::Connected ? "connected" :
                  next == ConnectionState::Connecting ? "connecting" :
                  next == ConnectionState::Disconnected ? "disconnected" : "unconfigured");
    drawStatus();
}

void startConnection() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    setState(ConnectionState::Connecting);
}
}  // namespace

void setup() {
    auto config = M5.config();
    config.serial_baudrate = 115200;
    config.internal_mic = false;
    config.internal_spk = false;
    M5.begin(config);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
    if (WIFI_SSID[0] == '\0') {
        setState(ConnectionState::Unconfigured);
        return;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Reconnect explicitly so failures also have a bounded retry interval.
    WiFi.setAutoReconnect(false);
    startConnection();
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    if (state != ConnectionState::Unconfigured) {
        if (WiFi.status() == WL_CONNECTED) {
            if (state != ConnectionState::Connected) {
                setState(ConnectionState::Connected);
            } else if (now - lastRefresh >= 5000) {
                drawStatus();
                lastRefresh = now;
            }
        } else if (state == ConnectionState::Connected ||
                   (state == ConnectionState::Connecting && now - stateSince >= kConnectTimeoutMs)) {
            WiFi.disconnect();
            setState(ConnectionState::Disconnected);
        } else if (state == ConnectionState::Disconnected && now - stateSince >= kRetryDelayMs) {
            startConnection();
        }
    }
    delay(20);
}
