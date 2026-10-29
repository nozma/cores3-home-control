#include "alexa_control.h"
#include "lighting.h"
#include "auto_control.h"
#define ESPALEXA_MAXDEVICES 2
#define ESPALEXA_NO_SUBPAGE
#include <Espalexa.h>
#include <ArduinoJson.h>

namespace {
Espalexa alexa;
WebServer server(80);
struct Pending { bool pending = false; bool pendingOn = false; int pendingBrightness = -1; bool followUp = false; };
Pending targets[2];
bool started = false;
uint32_t lastAttempt = 0;

EspalexaDevice kitchen("キッチン照明", DeviceCallbackFunction([](EspalexaDevice*) {}), EspalexaDeviceType::dimmable, 0);
EspalexaDevice living("リビング照明", DeviceCallbackFunction([](EspalexaDevice*) {}), EspalexaDeviceType::dimmable, 0);
EspalexaDevice* devices[2] = {&kitchen, &living};

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
String lightKey(int device) {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    uint32_t lower = strtoul(mac.substring(6).c_str(), nullptr, 16);
    return String(static_cast<int32_t>((lower << 7) | static_cast<uint32_t>(device)));
}

void lightDescription(JsonObject object, int n) {
    auto state = object.createNestedObject("state");
    state["on"] = devices[n]->getState();
    state["bri"] = devices[n]->getLastValue() - 1;
    state["reachable"] = true;
    state["alert"] = "none";
    object["type"] = "Dimmable light";
    object["name"] = devices[n]->getName();
    object["modelid"] = "LWB010";
    object["manufacturername"] = "Philips";
    String unique = WiFi.macAddress();
    unique += n == 0 ? "-01-00:11" : "-02-00:11";
    object["uniqueid"] = unique;
    object["swversion"] = "espalexa-2.7.0";
}

bool handleLightQuery(const String& uri) {
    if (server.method() != HTTP_GET || !uri.startsWith("/api/")) return false;
    int position = uri.indexOf("/lights");
    if (position < 0) return false;
    String suffix = uri.substring(position + 7);
    StaticJsonDocument<2048> response;
    if (suffix.length() == 0 || suffix == "/") {
        auto object = response.to<JsonObject>();
        for (int n = 0; n < 2; ++n) lightDescription(object.createNestedObject(lightKey(n)), n);
    } else {
        int n = suffix == "/" + lightKey(0) ? 0 : suffix == "/" + lightKey(1) ? 1 : -1;
        if (n < 0) return false;
        lightDescription(response.to<JsonObject>(), n);
    }
    sendJson(response);
    return true;
}

void handleRequest() {
    const String uri = server.uri();
    if (handleLightQuery(uri)) return;
    const int lights = uri.indexOf("/lights/");
    if (server.method() != HTTP_PUT || !uri.startsWith("/api/") ||
        lights < 0 || !uri.endsWith("/state")) {
        if (!alexa.handleAlexaApiCall(uri, server.arg("plain")))
            server.send(404, "text/plain", "Not found");
        return;
    }
    const String id = uri.substring(lights + 8, uri.length() - 6);
    const String address = "/lights/" + id + "/state";
    int n = id == lightKey(0) ? 0 : id == lightKey(1) ? 1 : -1;
    if (n < 0) { sendError(3, address, "resource not available"); return; }
    auto deviceId = static_cast<light::Id>(n);
    auto& device = *devices[n];
    auto& target = targets[n];
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
    const uint8_t value = hasBrightness ? request["bri"].as<int>() + 1 : device.getLastValue();
    const int brightness = on && hasBrightness ?
        (n == 0 ? light::alexaBrightnessStep(value) : light::livingStep(value)) : -1;
    automationManual(deviceId);
    const bool hadPending = target.pending;
    const bool queued = isLightingBusy(deviceId);
    const bool duplicate = queued && hadPending && on == target.pendingOn &&
        (brightness < 0 || brightness == target.pendingBrightness);
    bool accepted = queued || requestLighting(on ? light::Power::On : light::Power::Off, brightness, deviceId);
    Serial.printf("ALEXA_COMMAND: accepted=%d queued=%d duplicate=%d on=%d level=%d\n",
                  accepted, queued, duplicate, on, brightness);
    if (!accepted) { sendError(901, address, "device busy or state unconfirmed"); return; }
    if (queued && !duplicate) target.followUp = true;
    target.pending = isLightingBusy(deviceId);
    // A separate on-only request must not discard a brightness target already accepted.
    if (!queued || !hadPending || hasBrightness || !on) target.pendingBrightness = brightness;
    target.pendingOn = on;
    if (hasBrightness) device.setValue(value);
    device.setState(on);

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
            alexa.addDevice(&living);
            added = true;
        }
        server.onNotFound(handleRequest);
        started = alexa.begin(&server);
        Serial.printf("ALEXA_READY: %d\n", started);
    }
    if (!started) return;
    alexa.loop();
    for (int n = 0; n < 2; ++n) {
        auto id = static_cast<light::Id>(n);
        auto& target = targets[n];
        auto& device = *devices[n];
        if (target.pending && isLightingBusy(id)) continue;
        if (target.followUp) {
            target.followUp = false;
            bool accepted = requestLighting(target.pendingOn ? light::Power::On : light::Power::Off,
                                            target.pendingBrightness, id);
            Serial.printf("ALEXA_FOLLOWUP: device=%d accepted=%d\n", n, accepted);
            if (accepted && isLightingBusy(id)) continue;
        }
        target.pending = false;
        const auto state = getLightingState(id);
        if (state.power != light::Power::Unknown) {
            int percent = state.brightness < 0 ? -1 : n == 0 ? (state.brightness + 1) * 20 : state.brightness * 20;
            uint8_t level = percent < 0 ? device.getLastValue() : (percent * 255 + 50) / 100;
            device.setValue(level == 0 ? (percent == 0 ? 1 : 255) : level);
            device.setState(state.power == light::Power::On);
        }
    }
}
