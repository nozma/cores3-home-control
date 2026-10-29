#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

struct EnvironmentSample {
    float values[4]{};
    char timestamps[4][25]{};
    bool available[4]{};
    int code = 0;
};

// Match the existing sensor Remo explicitly; never choose an arbitrary account device.
inline EnvironmentSample parseEnvironment(JsonArray devices) {
    EnvironmentSample sample;
    sample.code = -104;
    JsonObject target;
    unsigned matches = 0;
    for (JsonObject device : devices) {
        if (std::strcmp(device["name"] | "", "remo") == 0) {
            target = device;
            ++matches;
        }
    }
    if (matches != 1) return sample;
    sample.code = 200;
    const char* keys[] = {"te", "hu", "il", "mo"};
    for (int i = 0; i < 4; ++i) {
        JsonObject event = target["newest_events"][keys[i]];
        if (!event["val"].is<float>() && !event["val"].is<int>()) continue;
        float value = event["val"].as<float>();
        if (!std::isfinite(value)) continue;
        sample.available[i] = true;
        sample.values[i] = value;
        const char* stamp = event["created_at"] | "";
        std::strncpy(sample.timestamps[i], stamp, sizeof(sample.timestamps[i]) - 1);
    }
    return sample;
}
