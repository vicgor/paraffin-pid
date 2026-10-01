#include "Safety.h"

namespace ctl {

const char* faultName(Fault f) {
    switch (f) {
        case Fault::None: return "OK";
        case Fault::SensorProcess: return "SENSOR";
        case Fault::SensorHeater: return "HTR SENS";
        case Fault::OverTempProcess: return "OVERTEMP";
        case Fault::OverTempHeater: return "HTR HOT";
        case Fault::NoHeatRise: return "NO RISE";
        case Fault::IdleHeating: return "SSR STUCK";
        case Fault::TuneFailed: return "TUNE ERR";
        case Fault::WatchdogReset: return "WDT RST";
    }
    return "?";
}

const char* faultText(Fault f) {
    switch (f) {
        case Fault::None: return "Нет ошибок";
        case Fault::SensorProcess: return "Ошибка датчика парафина";
        case Fault::SensorHeater: return "Ошибка датчика ТЭНа";
        case Fault::OverTempProcess: return "Перегрев парафина";
        case Fault::OverTempHeater: return "Перегрев ТЭНа";
        case Fault::NoHeatRise: return "Нагрев включён, температура не растёт";
        case Fault::IdleHeating: return "Температура растёт при выключенном нагреве (SSR залипло?)";
        case Fault::TuneFailed: return "Автотюнинг не удался";
        case Fault::WatchdogReset: return "Перезагрузка по сторожевому таймеру";
    }
    return "?";
}

void SafetyMonitor::reset(uint32_t nowMs) {
    rtActive_ = false;
    idleActive_ = false;
    rtStart_ = idleStart_ = nowMs;
}

Fault SafetyMonitor::check(const SafetyInputs& in, const ControlSettings& s, uint32_t nowMs) {
    // 1. Датчики
    if (in.procFails >= s.sensorFailLimit) return Fault::SensorProcess;
    if (s.heaterSensorRequired && !in.heaterPresent) return Fault::SensorHeater;
    if (in.heaterPresent && in.heaterFails >= s.sensorFailLimit) return Fault::SensorHeater;

    // 2. Перегрев — проверяется всегда, даже когда нагрев выключен
    if (in.procValid && in.procC >= s.maxProcessC) return Fault::OverTempProcess;
    if (in.heaterPresent && in.heaterValid && in.heaterC >= s.maxHeaterC) return Fault::OverTempHeater;

    if (!in.procValid) return Fault::None;  // единичный сбой чтения — ждём следующего

    const bool hv = in.heaterPresent && in.heaterValid;

    // 3. Нагрев включён, температура ниже цели, средняя мощность за окно
    //    заметная, а роста нет (датчик выпал из ёмкости, ТЭН оборван, сгорело SSR).
    //    Окно должно быть больше плато плавления (скрытая теплота!) — по умолчанию 30 мин.
    if (in.heatingEnabled && s.runawayWindowMin > 0) {
        if (!rtActive_) {
            rtActive_ = true;
            rtStart_ = nowMs;
            rtProc0_ = in.procC;
            rtHeater0_ = hv ? in.heaterC : 0;
            rtOutSum_ = 0;
            rtN_ = 0;
        } else {
            rtOutSum_ += in.outputPct;
            rtN_++;
            if (nowMs - rtStart_ >= (uint32_t)(s.runawayWindowMin * 60000.0f)) {
                float rise = in.procC - rtProc0_;
                if (hv && in.heaterC - rtHeater0_ > rise) rise = in.heaterC - rtHeater0_;
                const float avgOut = rtN_ ? rtOutSum_ / rtN_ : 0;
                const bool belowTarget = in.procC < in.targetC - 2.0f;
                if (belowTarget && avgOut >= s.runawayMinOutPct && rise < s.runawayMinRiseC)
                    return Fault::NoHeatRise;
                rtActive_ = false;  // окно пройдено — начнётся следующее
            }
        }
    } else {
        rtActive_ = false;
    }

    // 4. Нагрев выключен, а температура растёт → SSR пробито/залипло
    if (!in.heatingEnabled && s.idleRiseC > 0) {
        const float t = hv && in.heaterC > in.procC ? in.heaterC : in.procC;
        if (!idleActive_) {
            idleActive_ = true;
            idleStart_ = nowMs;
            idleMin_ = t;
        } else {
            if (t < idleMin_) idleMin_ = t;
            if (nowMs - idleStart_ > 60000u && t - idleMin_ > s.idleRiseC) return Fault::IdleHeating;
        }
    } else {
        idleActive_ = false;
    }
    return Fault::None;
}

}  // namespace ctl
