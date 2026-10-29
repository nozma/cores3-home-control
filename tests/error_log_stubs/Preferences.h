#pragma once
#include <map>
#include <string>
#include <cstring>
inline std::map<std::string, std::string> fakeNvs;
inline bool fakeWriteFailure = false;
class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    size_t getBytesLength(const char* key) { return fakeNvs[key].size(); }
    size_t getBytes(const char* key, void* out, size_t size) {
        const auto& value = fakeNvs[key];
        if (value.size() != size) return 0;
        memcpy(out, value.data(), size); return size;
    }
    size_t putBytes(const char* key, const void* data, size_t size) {
        if (fakeWriteFailure) return 0;
        fakeNvs[key] = std::string(static_cast<const char*>(data), size); return size;
    }
};
