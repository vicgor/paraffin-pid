#pragma once
// Хранение настроек во флеше: ESP32 — NVS (Preferences), RP2350 — эмуляция EEPROM.
#include <Arduino.h>

#include "ControlSettings.h"

struct NetSettings {
    char ssid[33] = "";
    char pass[65] = "";
    char hostname[33] = "paraffin";
    char apPass[33] = "paraffin123";  // пароль точки доступа (≥ 8 символов)
    char webPass[33] = "";            // пароль веб-интерфейса (логин admin), пусто — без пароля
    char mqttHost[65] = "";
    uint16_t mqttPort = 1883;
    char mqttUser[33] = "";
    char mqttPass[65] = "";
    char mqttBase[48] = "";  // базовый топик, пусто — paraffin/<id>
    uint8_t mqttHaDiscovery = 1;
};

struct AppSettings {
    ctl::ControlSettings ctl;
    NetSettings net;
    uint8_t procAddr[8] = {};    // адрес DS18B20 в парафине
    uint8_t heaterAddr[8] = {};  // адрес DS18B20 на ТЭНе
    uint8_t buzzer = 1;
};

namespace settings_store {
bool load(AppSettings& s);  // false — загружены значения по умолчанию
bool save(const AppSettings& s);
void factoryReset();
}  // namespace settings_store
