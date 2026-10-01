#pragma once
// Программные защиты. НЕ заменяют аппаратную защиту
// (термовыключатель KSD301 / термопредохранитель последовательно с ТЭНом)!
#include <stdint.h>

#include "ControlSettings.h"

namespace ctl {

enum class Fault : uint8_t {
    None = 0,
    SensorProcess,    // нет связи / ошибка датчика парафина
    SensorHeater,     // нет датчика ТЭНа (если он обязателен)
    OverTempProcess,  // перегрев парафина
    OverTempHeater,   // перегрев ТЭНа
    NoHeatRise,       // нагрев включён, а температура не растёт (датчик выпал, ТЭН сгорел)
    IdleHeating,      // нагрев выключен, а температура растёт (залипло SSR)
    TuneFailed,       // автотюнинг не удался
    WatchdogReset,    // был сброс по сторожевому таймеру
};

const char* faultName(Fault f);       // короткое (для дисплея)
const char* faultText(Fault f);       // по-русски (веб/консоль)

struct SafetyInputs {
    bool procValid = false;
    float procC = 0;
    uint16_t procFails = 0;  // подряд неудачных чтений
    bool heaterPresent = false;
    bool heaterValid = false;
    float heaterC = 0;
    uint16_t heaterFails = 0;
    float outputPct = 0;     // текущий запрос мощности
    float targetC = 0;
    bool heatingEnabled = false;
};

class SafetyMonitor {
public:
    void reset(uint32_t nowMs);
    Fault check(const SafetyInputs& in, const ControlSettings& s, uint32_t nowMs);

private:
    bool rtActive_ = false;
    uint32_t rtStart_ = 0;
    float rtProc0_ = 0, rtHeater0_ = 0;
    float rtOutSum_ = 0;
    uint32_t rtN_ = 0;
    bool idleActive_ = false;
    uint32_t idleStart_ = 0;
    float idleMin_ = 0;
};

}  // namespace ctl
