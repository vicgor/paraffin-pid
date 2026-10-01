#pragma once
// Релейный автотюнинг (метод Острёма–Хэгглунда).
// Регулятор заменяется реле с гистерезисом вокруг уставки; объект входит
// в автоколебания. По амплитуде a и периоду Tu колебаний:
//   Ku = 4·d / (π·sqrt(a² − h²)),  d = (outHigh − outLow)/2,  h — гистерезис.
// Далее Kp/Ki/Kd считаются по выбранному правилу.
#include <stdint.h>

#include "Pid.h"

namespace ctl {

enum class TuneRule : uint8_t {
    ZieglerNichols = 0,  // агрессивно, заметное перерегулирование
    TyreusLuyben = 1,    // по умолчанию: мягко, хорошо для тепловых объектов
    NoOvershoot = 2,     // минимум перерегулирования
};

const char* tuneRuleName(TuneRule r);
PidGains gainsFromUltimate(float ku, float tuSec, TuneRule rule);

class RelayAutotune {
public:
    struct Config {
        float setpoint = 70;
        float hysteresis = 0.5f;  // °C
        float outHigh = 100;      // %
        float outLow = 0;         // %
        uint8_t cyclesToMeasure = 3;  // сколько циклов усреднять (после первого переходного)
        uint8_t maxCycles = 12;
        uint32_t timeoutSec = 4 * 3600;
        TuneRule rule = TuneRule::TyreusLuyben;
    };

    enum class State : uint8_t { Idle, Running, Done, Failed };

    void start(const Config& cfg, float pv, uint32_t nowMs);
    void abort() { state_ = State::Idle; }

    // Возвращает требуемый выход, %
    float update(float pv, uint32_t nowMs);

    State state() const { return state_; }
    bool active() const { return state_ == State::Running; }
    uint8_t cycles() const { return cycles_; }
    float ku() const { return ku_; }
    float tuSec() const { return tu_; }
    PidGains result() const { return gainsFromUltimate(ku_, tu_, cfg_.rule); }
    const char* failReason() const { return fail_; }
    float output() const { return out_; }

private:
    void finish();

    static constexpr int kMaxStore = 16;
    Config cfg_;
    State state_ = State::Idle;
    bool high_ = true;
    uint32_t t0_ = 0;
    uint32_t lastRiseSwitch_ = 0;  // момент переключения low→high
    bool haveRiseSwitch_ = false;
    float phaseMin_ = 0, phaseMax_ = 0;
    float lastMin_ = 0;
    bool haveMin_ = false;
    float periods_[kMaxStore] = {};
    float amps_[kMaxStore] = {};
    uint8_t cycles_ = 0;
    float ku_ = 0, tu_ = 0, out_ = 0;
    const char* fail_ = "";
};

}  // namespace ctl
