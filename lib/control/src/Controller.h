#pragma once
// Конечный автомат регулятора: Ожидание / Нагрев / Автотюнинг / Авария.
// Не знает ни про железо, ни про потоки — только логика (тестируется на ПК).
#include <stdint.h>

#include "Autotune.h"
#include "ControlSettings.h"
#include "Pid.h"
#include "Safety.h"
#include "SetpointRamp.h"

namespace ctl {

enum class Mode : uint8_t { Idle = 0, Run, Tune, Fault };
const char* modeName(Mode m);

struct Measurement {
    bool procValid = false;
    float procC = 0;
    uint16_t procFails = 0;
    bool heaterPresent = false;
    bool heaterValid = false;
    float heaterC = 0;
    uint16_t heaterFails = 0;
};

enum Event : uint8_t {
    EV_AT_TEMP = 1 << 0,    // впервые вышли на уставку
    EV_TUNE_DONE = 1 << 1,  // автотюнинг завершён, коэффициенты применены
    EV_FAULT = 1 << 2,      // авария
};

struct ControllerStatus {
    Mode mode = Mode::Idle;
    Fault fault = Fault::None;
    Measurement meas;
    float targetC = 0;  // заданная уставка
    float spC = 0;      // текущая (с рампой)
    float outPct = 0;
    float p = 0, i = 0, d = 0;
    bool atTemp = false;
    uint8_t tuneCycles = 0;
    float ku = 0, tu = 0;
    uint32_t modeSinceMs = 0;
};

class Controller {
public:
    void begin(const ControlSettings& s, uint32_t nowMs);
    void applySettings(const ControlSettings& s);
    const ControlSettings& settings() const { return s_; }

    bool start(uint32_t nowMs);
    void stop(uint32_t nowMs);
    bool startTune(uint32_t nowMs);
    bool ack(uint32_t nowMs);
    void setTarget(float c);
    void raiseFault(Fault f, uint32_t nowMs);

    // Вызывать на каждом новом измерении (≈1 раз/с). Возвращает мощность, %.
    float step(const Measurement& m, uint32_t nowMs);

    const ControllerStatus& status() const { return st_; }
    float output() const { return st_.outPct; }

    uint8_t takeEvents() { uint8_t e = events_; events_ = 0; return e; }
    // true, если контроллер сам изменил настройки (результат тюнинга и т.п.) — их надо сохранить
    bool takeSettingsDirty() { bool d = dirty_; dirty_ = false; return d; }

private:
    void setMode(Mode m, uint32_t nowMs);
    void enterFault(Fault f, uint32_t nowMs);
    SafetyInputs safetyInputs(const Measurement& m) const;

    ControlSettings s_;
    Pid pid_;
    SetpointRamp ramp_;
    RelayAutotune tune_;
    SafetyMonitor safety_;
    ControllerStatus st_;
    uint32_t lastStepMs_ = 0;
    bool havePrev_ = false;
    bool atTempEventSent_ = false;
    uint8_t events_ = 0;
    bool dirty_ = false;
};

}  // namespace ctl
