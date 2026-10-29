#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
struct FakeSerial {
    std::string input, output;
    int available() { return input.size(); }
    int read() { char c = input.front(); input.erase(0, 1); return c; }
    void println(const char* s) { output += std::string(s) + "\n"; }
    void printf(const char* format, ...) {
        char buffer[1024]; va_list args; va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); output += buffer;
    }
};
inline FakeSerial Serial;
