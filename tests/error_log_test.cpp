#include <cassert>
#include "../src/error_log.cpp"

void command(const std::string& input) {
    Serial.output.clear(); Serial.input = input;
    while (Serial.available()) updateErrorLogConsole();
}
int main() {
    beginErrorLog();
    command("errorlog\r\n");
    assert(Serial.output.find("count=0") != std::string::npos);
    LightingError r;
    r.code = 200;
    saveLightingError(r);
    assert(newest == 0);
    r.code = 400;
    for (unsigned i = 1; i <= 25; ++i) { r.uptimeMs = i; saveLightingError(r); }
    command("errorlog\n");
    assert(Serial.output.find("count=20") != std::string::npos);
    assert(Serial.output.find("seq=5 ") == std::string::npos);
    assert(Serial.output.find("seq=6 ") != std::string::npos);
    assert(Serial.output.find("seq=25 ") != std::string::npos);
    // Simulate loss of all volatile log state at reboot, preserving NVS.
    newest = 0; count = 0; ready = false;
    beginErrorLog();
    assert(newest == 25 && count == 20);
    r.code = -11;
    saveLightingError(r);
    assert(newest == 26);
    fakeWriteFailure = true;
    saveLightingError(r);
    assert(newest == 26);
    fakeWriteFailure = false;
    command(std::string(30, 'x') + "errorlog\n");
    assert(Serial.output.empty());
    command("errorlog\n");
    assert(Serial.output.find("seq=26 ") != std::string::npos);
    assert(Serial.output.find("code=-11") != std::string::npos);
    // Truncated records do not get interpreted as valid errors on boot.
    fakeNvs["e00"] = "bad";
    newest = 0; count = 0; ready = false;
    beginErrorLog();
    assert(count == 19);
    puts("error_log tests passed");
}
