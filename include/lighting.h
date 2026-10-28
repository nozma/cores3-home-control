#pragma once
#include "light_model.h"

void beginLighting();
void updateLighting();
void redrawLighting();
light::State getLightingState();
bool requestLighting(light::Power power, int brightness = -1);
bool isLightingBusy();
