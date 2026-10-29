#include "environment_model.h"
#include <cassert>
#include <iostream>

EnvironmentSample parse(const char* json) {
    DynamicJsonDocument doc(4096);
    assert(!deserializeJson(doc, json));
    return parseEnvironment(doc.as<JsonArray>());
}
int main() {
    auto sample = parse(R"([{"name":"Remo E","newest_events":{}},{"name":"remo","newest_events":{"te":{"val":24.5,"created_at":"2026-10-29T08:17:15Z"},"hu":{"val":44},"il":{"val":0},"mo":{"val":1,"created_at":"2026-10-29T09:54:02Z"}}}])");
    assert(sample.code == 200);
    assert(sample.values[0] == 24.5f && sample.values[1] == 44);
    assert(sample.available[2] && sample.values[2] == 0);
    assert(sample.available[3] && sample.values[3] == 1);
    assert(std::strcmp(sample.timestamps[0], "2026-10-29T08:17:15Z") == 0);
    assert(std::strcmp(sample.timestamps[3], "2026-10-29T09:54:02Z") == 0);
    assert(sample.timestamps[1][0] == '\0');
    sample = parse(R"([{"name":"remo","newest_events":{"te":{"val":null},"hu":{"val":"44"},"il":{"val":false}}}])");
    assert(sample.code == 200);
    for (bool available : sample.available) assert(!available);
    assert(parse(R"([{"name":"Remo E"}])").code == -104);
    assert(parse(R"([{"name":"remo"},{"name":"remo"}])").code == -104);
    std::cout << "environment_model_test passed\n";
}
