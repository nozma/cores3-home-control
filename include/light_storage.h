#pragma once
#include "light_model.h"

void beginLightStorage();
bool loadLightState(light::Id id, light::State& state, bool& normal);
bool saveLightState(light::Id id, const light::State& state, bool normal);
// Persist a conservative snapshot before the worker can emit any signal.
bool prepareLightState(light::Id id, light::State state, bool normal, const light::Plan& plan);
