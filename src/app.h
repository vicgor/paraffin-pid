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

#if SIM_SENSORS
// --- Симулятор (только sim-сборка). Команды применяет control task,
// поэтому модель меняется без гонок с циклом управления.
namespace sim {
constexpr float kSpeedMin = 0.1f, kSpeedMax = 100.0f;
constexpr float kTempMinC = -20.0f, kTempMaxC = 150.0f;
constexpr float kPowerMaxW = 3000.0f;
}  // namespace sim

struct SimCmd {
    enum Kind : uint8_t { None, Speed, Wax, Heater, Amb, Power, Fail, Bus, Stuck, Reset, Hang };
    Kind kind = None;
    float v = 0;
};

struct SimState {
    float waxC = 0, heaterC = 0, ambientC = 0, powerW = 0, meltedPct = 0, speed = 1;
    bool failProc = false, hangBus = false, stuck = false;
};

bool simCmd(const SimCmd& c);  // false — предыдущая команда ещё не применена
SimState simState();           // снимок модели на последнем такте управления
#endif

}  // namespace app
