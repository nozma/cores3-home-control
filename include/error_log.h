#pragma once
#include <cstdint>

struct LightingError {
    uint32_t version = 1;
    uint32_t sequence = 0;
    int64_t unixTime = 0; // Zero until the clock is synchronized.
    uint32_t uptimeMs = 0;
    uint32_t elapsedMs = 0;
    int32_t gapMs = -1; // -1 if there was no previous attempt in this boot.
    int32_t code = 0;
    int32_t rssi = 0;
    uint8_t device = 0;
    uint8_t signal = 0;
    uint8_t local = 0;
    uint8_t completed = 0;
    uint8_t total = 0;
};
void beginErrorLog();
void saveLightingError(LightingError record);
void updateErrorLogConsole();
