#pragma once
#include "light_model.h"

void beginLighting();
void updateLighting();
void redrawLighting();
light::State getLightingState(light::Id id = light::Id::Kitchen);
bool requestLighting(light::Power power, int brightness = -1, light::Id id = light::Id::Kitchen);
bool isLightingBusy(light::Id id = light::Id::Kitchen);
bool requestLivingPercent(light::Axis axis, int step);
