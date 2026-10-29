#include "error_log.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

namespace {
constexpr unsigned kCapacity = 20;
Preferences storage;
bool ready = false;
uint32_t newest = 0;
unsigned count = 0;

bool readSlot(unsigned slot, LightingError& record) {
    char key[8];
    snprintf(key, sizeof(key), "e%02u", slot);
    return storage.getBytesLength(key) == sizeof(record) &&
        storage.getBytes(key, &record, sizeof(record)) == sizeof(record) &&
        record.version == 1 && record.sequence > 0;
}
void show() {
    if (!ready) { Serial.println("ERROR_LOG: storage unavailable"); return; }
    Serial.printf("ERROR_LOG_BEGIN: count=%u capacity=%u time=unix_utc(0=unsynced) gap_ms=-1(no_previous_attempt)\n", count, kCapacity);
    // Each entry is one atomic NVS value; no separate cursor needs updating.
    for (uint32_t seq = newest >= kCapacity ? newest - kCapacity + 1 : 1; seq <= newest && seq > 0; ++seq) {
        LightingError r;
        if (!readSlot((seq - 1) % kCapacity, r) || r.sequence != seq) continue;
        Serial.printf("ERROR_LOG: seq=%lu time=%lld uptime_ms=%lu route=%s device=%u signal=%u code=%ld elapsed_ms=%lu gap_ms=%ld rssi=%ld completed=%u/%u\n",
            static_cast<unsigned long>(r.sequence), static_cast<long long>(r.unixTime),
            static_cast<unsigned long>(r.uptimeMs), r.local ? "local" : "cloud",
            r.device, r.signal, static_cast<long>(r.code), static_cast<unsigned long>(r.elapsedMs),
            static_cast<long>(r.gapMs), static_cast<long>(r.rssi), r.completed, r.total);
    }
    Serial.println("ERROR_LOG_END");
}
}
void beginErrorLog() {
    ready = storage.begin("light-errors", false);
    if (ready) {
        for (unsigned slot = 0; slot < kCapacity; ++slot) {
            LightingError r;
            if (readSlot(slot, r)) { ++count; if (r.sequence > newest) newest = r.sequence; }
        }
    }
    Serial.printf("ERROR_LOG_READY: available=%d count=%u command=errorlog\n", ready, count);
}
void saveLightingError(LightingError r) {
    if (r.code >= 200 && r.code < 300) return;
    if (!ready || newest == UINT32_MAX) { Serial.println("ERROR_LOG: save unavailable"); return; }
    r.version = 1;
    r.sequence = newest + 1;
    char key[8];
    snprintf(key, sizeof(key), "e%02u", (r.sequence - 1) % kCapacity);
    if (storage.putBytes(key, &r, sizeof(r)) != sizeof(r)) {
        Serial.println("ERROR_LOG: save failed");
        return;
    }
    newest = r.sequence;
    if (count < kCapacity) ++count;
    Serial.printf("ERROR_LOG_SAVED: seq=%lu\n", static_cast<unsigned long>(newest));
}
void updateErrorLogConsole() {
    static char command[24];
    static unsigned length = 0;
    static bool overflow = false;
    // Bound serial work per frame; arbitrary input is never echoed or saved.
    for (unsigned n = 0; n < 64 && Serial.available(); ++n) {
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            command[length] = '\0';
            if (!overflow && strcmp(command, "errorlog") == 0) show();
            length = 0;
            overflow = false;
        } else if (length < sizeof(command) - 1) command[length++] = c;
        else overflow = true;
    }
}
