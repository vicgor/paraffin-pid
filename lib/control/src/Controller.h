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

// Почему ack() сейчас откажет (None — сброс разрешён).
enum class AckBlock : uint8_t {
    None = 0,
    SensorProcess,  // нет N достоверных измерений датчика парафина подряд
    SensorHeater,   // нет N достоверных измерений датчика ТЭНа подряд / датчик обязателен и не найден
    ProcessHot,     // парафин не остыл ниже maxProcessC − гистерезис
    HeaterHot,      // ТЭН не остыл ниже maxHeaterC − гистерезис
    StillRising,    // SSR STUCK: температура ещё растёт или выдержка не истекла
    SafetyCheck,    // текущая проверка SafetyMonitor обнаружила аварию
};
const char* ackBlockName(AckBlock b);  // латиница (дисплей)
const char* ackBlockText(AckBlock b);  // по-русски (веб/консоль/MQTT)

// Условия сброса аварии (см. docs/control.md, «Что проверяет ack»).
namespace ackcfg {
constexpr uint16_t kValidReadings = 5;          // достоверных измерений подряд
constexpr float kHystC = 5.0f;                  // гистерезис порогов перегрева, °C
constexpr uint32_t kStuckHoldMs = 5u * 60000u;  // SSR STUCK: выдержка без роста
constexpr float kStuckRiseC = 0.5f;             // рост выше этого перезапускает выдержку
}  // namespace ackcfg

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
    // Сброс аварии. false — причина не устранена, подробности в lastAckBlock().
    bool ack(uint32_t nowMs);
    // Что мешает сбросу аварии прямо сейчас (None вне FAULT или если сброс разрешён).
    AckBlock ackBlock(uint32_t nowMs) const;
    AckBlock lastAckBlock() const { return lastAckBlock_; }
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

    // История для ack(): копится в step() всегда, выдержка SSR STUCK — только в FAULT
    uint16_t procValidRun_ = 0;    // достоверных измерений парафина подряд
    uint16_t heaterValidRun_ = 0;  // достоверных измерений ТЭНа подряд
    bool stuckHaveRef_ = false;
    float stuckRefC_ = 0;
    uint32_t stuckRefMs_ = 0;  // с этого момента нет роста температуры
    AckBlock lastAckBlock_ = AckBlock::None;
};

}  // namespace ctl
