#pragma once
// Потокобезопасный фасад над регулятором. Вызывается из loop() (UI, веб, MQTT, CLI);
// сам регулятор и SSR работают в отдельном контексте (задача FreeRTOS / второе ядро RP2350).
#include <Arduino.h>

#include "Controller.h"
#include "settings_store.h"

#define FW_VERSION "1.0.0"

namespace app {

struct SensorInfo {
    uint8_t count = 0;
    bool procPresent = false, heaterPresent = false;
    char procAddr[17] = "", heaterAddr[17] = "";
};

void begin();       // из setup()
void loopTick();    // из loop(): отложенное сохранение настроек

ctl::ControllerStatus status();
ctl::ControlSettings controlSettings();
void applyControlSettings(const ctl::ControlSettings& s);  // + сохранение
bool heaterOn();

bool start();
void stop();
bool startTune();
bool ack();
void setTarget(float c);
uint8_t takeEvents();

SensorInfo sensorInfo();
void swapSensors();
void rescanSensors();

// Сетевые/прочие настройки — только из контекста loop()
AppSettings& settings();
void requestSave();
void saveNow();
void factoryReset();

const char* deviceId();  // "a1b2c3d4"

}  // namespace app
