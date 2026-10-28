#include "alexa_control.h"
#include "lighting.h"
#define ESPALEXA_MAXDEVICES 1
#define ESPALEXA_NO_SUBPAGE
#include <Espalexa.h>
#include <ArduinoJson.h>

namespace {
Espalexa alexa;
WebServer server(80);
bool pending = false;
bool pendingOn = false;
int pendingBrightness = -1;
bool followUp = false;
bool started = false;
uint32_t lastAttempt = 0;

EspalexaDevice kitchen("キッチン照明", DeviceCallbackFunction([](EspalexaDevice*) {}), EspalexaDeviceType::dimmable, 0);

void sendJson(const JsonDocument& document) {
    String body;
    serializeJson(document, body);
    server.send(200, "application/json", body);
}

void sendError(int type, const String& address, const char* description) {
    StaticJsonDocument<384> response;
    auto error = response.to<JsonArray>().createNestedObject().createNestedObject("error");
    error["type"] = type;
    error["address"] = address;
    error["description"] = description;
    sendJson(response);
}

// Match Espalexa's stable light key so existing Alexa registration is preserved.
String lightKey() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    uint32_t lower = strtoul(mac.substring(6).c_str(), nullptr, 16);
    return String(static_cast<int32_t>(lower << 7));
}

void handleRequest() {
    const String uri = server.uri();
    const int lights = uri.indexOf("/lights/");
    if (server.method() != HTTP_PUT || !uri.startsWith("/api/") ||
        lights < 0 || !uri.endsWith("/state")) {
        if (!alexa.handleAlexaApiCall(uri, server.arg("plain")))
            server.send(404, "text/plain", "Not found");
        return;
    }
    const String id = uri.substring(lights + 8, uri.length() - 6);
    const String address = "/lights/" + id + "/state";
    if (id != lightKey()) { sendError(3, address, "resource not available"); return; }
    StaticJsonDocument<512> request;
    const String body = server.arg("plain");
    if (body.length() > 512 || deserializeJson(request, body) || !request.is<JsonObject>()) {
        sendError(2, address, "invalid JSON"); return;
    }
    bool hasOn = request.containsKey("on");
    bool hasBrightness = request.containsKey("bri");
    if ((!hasOn && !hasBrightness) || (hasOn && !request["on"].is<bool>()) ||
        (hasBrightness && (!request["bri"].is<int>() || request["bri"].as<int>() < 0 ||
                           request["bri"].as<int>() > 254))) {
        sendError(7, address, "invalid value"); return;
    }
    const bool on = hasOn ? request["on"].as<bool>() : true;
    const uint8_t value = hasBrightness ? request["bri"].as<int>() + 1 : kitchen.getLastValue();
    const int brightness = on && hasBrightness ? light::alexaBrightnessStep(value) : -1;
    const bool queued = pending && isLightingBusy();
    const bool duplicate = queued && on == pendingOn &&
        (brightness < 0 || brightness == pendingBrightness);
    bool accepted = queued || requestLighting(on ? light::Power::On : light::Power::Off, brightness);
    Serial.printf("ALEXA_COMMAND: accepted=%d queued=%d duplicate=%d on=%d level=%d\n",
                  accepted, queued, duplicate, on, brightness);
    if (!accepted) { sendError(901, address, "device busy or state unconfirmed"); return; }
    if (queued && !duplicate) followUp = true;
    pending = isLightingBusy();
    // A separate on-only request must not discard a brightness target already accepted.
    if (!queued || hasBrightness || !on) pendingBrightness = brightness;
    pendingOn = on;
    if (hasBrightness) kitchen.setValue(value);
    kitchen.setState(on);

    // Hue success is per attribute and uses the actual requested light ID.
    StaticJsonDocument<512> response;
    auto array = response.to<JsonArray>();
    if (hasOn) array.createNestedObject().createNestedObject("success")[address + "/on"] = on;
    if (hasBrightness) array.createNestedObject().createNestedObject("success")[address + "/bri"] = request["bri"].as<int>();
    sendJson(response);
}
}  // namespace

void updateAlexa() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!started) {
        if (millis() - lastAttempt < 5000) return;
        lastAttempt = millis();
        static bool added = false;
        if (!added) {
            alexa.addDevice(&kitchen);
            added = true;
        }
        server.onNotFound(handleRequest);
        started = alexa.begin(&server);
        Serial.printf("ALEXA_READY: %d\n", started);
    }
    if (!started) return;
    alexa.loop();
    // Alexa may split an off->brightness command into on and bri requests.
    // Hold the latest target until the active IR sequence has completed.
    if (pending && isLightingBusy()) return;
    if (followUp) {
        followUp = false;
        bool accepted = requestLighting(pendingOn ? light::Power::On : light::Power::Off,
                                        pendingBrightness);
        Serial.printf("ALEXA_FOLLOWUP: accepted=%d\n", accepted);
        if (accepted && isLightingBusy()) return;
    }
    pending = false;
    const auto state = getLightingState();
    // Alexa has no 'unknown' light state; only publish estimates when known.
    if (state.power != light::Power::Unknown) {
        uint8_t level = state.brightness < 0 ? kitchen.getLastValue()
            : static_cast<uint8_t>((state.brightness + 1) * 51);
        kitchen.setValue(level == 0 ? 255 : level);
        kitchen.setState(state.power == light::Power::On);
    }
}
