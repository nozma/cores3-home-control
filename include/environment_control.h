#pragma once
#include "environment_model.h"
#include "environment_history.h"
#include <M5GFX.h>

void beginEnvironmentHistory();
bool updateEnvironmentHistory(uint32_t now);
void acceptEnvironmentSample(const EnvironmentSample& sample,uint32_t now);
void serviceEnvironmentStorage();
String drawEnvironmentGraph(lgfx::LGFXBase& d,const EnvironmentSample& sample,int code,bool connected,bool brightness);
