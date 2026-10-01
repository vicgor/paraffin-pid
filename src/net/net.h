#pragma once
// Wi-Fi (STA + резервная точка доступа), веб-интерфейс, JSON API, MQTT (+ Home Assistant).
// На платах без Wi-Fi (Pico 2) все функции — пустые заглушки.
#include <Arduino.h>

namespace net {

void begin();
void loop();

enum class State : uint8_t { Off, Connecting, Connected, AccessPoint };
State state();
bool mqttConnected();
void statusLine(char* out, size_t n);  // короткая строка для дисплея
void ipString(char* out, size_t n);
int rssi();

}  // namespace net
