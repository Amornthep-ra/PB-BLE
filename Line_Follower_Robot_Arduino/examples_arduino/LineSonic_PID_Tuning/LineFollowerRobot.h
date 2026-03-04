#pragma once
#include <Arduino.h>

void LFR_begin(const char* deviceName);
void LFR_poll();
bool LFR_isConnected();
bool LFR_available();
bool LFR_readLine(String &out);
void LFR_sendLine(const String &msg);
