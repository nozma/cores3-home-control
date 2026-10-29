#pragma once
#include <Arduino.h>
#include "auto_model.h"
#include "light_model.h"
void beginAutomation();
void updateAutomation();
const automation::Settings& automationSettings();
bool changeAutomation(int field, int direction);
String automationStatus();
String automationNext();
void automationManual(light::Id id);

void automationResult(light::Id id, bool success);
