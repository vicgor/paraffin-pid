#include "Controller.h"

#include <math.h>
#include <string.h>

namespace ctl {

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool sanitize(ControlSettings& s) {
    ControlSettings o = s;
    // NaN → значения по умолчанию
    ControlSettings def;
    float* fs[] = {&s.targetC, &s.spMinC, &s.spMaxC, &s.rampCpm, &s.kp, &s.ki, &s.kd, &s.dFilterSec,
                   &s.outMaxPct, &s.tuneHystC, &s.tuneOutHighPct, &s.maxProcessC, &s.maxHeaterC,
                   &s.runawayWindowMin, &s.runawayMinRiseC, &s.runawayMinOutPct, &s.idleRiseC, &s.rampHoldC, &s.iZoneC};
    const float* fd[] = {&def.targetC, &def.spMinC, &def.spMaxC, &def.rampCpm, &def.kp, &def.ki, &def.kd,
                         &def.dFilterSec, &def.outMaxPct, &def.tuneHystC, &def.tuneOutHighPct,
                         &def.maxProcessC, &def.maxHeaterC, &def.runawayWindowMin, &def.runawayMinRiseC,
                         &def.runawayMinOutPct, &def.idleRiseC, &def.rampHoldC, &def.iZoneC};
    for (unsigned k = 0; k < sizeof(fs) / sizeof(fs[0]); ++k)
        if (isnan(*fs[k]) || isinf(*fs[k])) *fs[k] = *fd[k];

    // Верхние пределы — в ControlSettings.h. DS18B20 сам по себе читается только до 125 °C.
    s.maxProcessC = clampf(s.maxProcessC, 30, kMaxProcessLimitC);
    s.maxHeaterC = clampf(s.maxHeaterC, 30, kMaxHeaterLimitC);
    s.spMinC = clampf(s.spMinC, 0, 100);
    s.spMaxC = clampf(s.spMaxC, s.spMinC + 1, s.maxProcessC - 2);
    s.targetC = clampf(s.targetC, s.spMinC, s.spMaxC);
    s.rampCpm = clampf(s.rampCpm, 0, 60);
    s.rampHoldC = clampf(s.rampHoldC, 0, 50);
    s.kp = clampf(s.kp, 0, 1000);
    s.ki = clampf(s.ki, 0, 100);
    s.kd = clampf(s.kd, 0, 100000);
    s.dFilterSec = clampf(s.dFilterSec, 0, 600);
    s.iZoneC = clampf(s.iZoneC, 0, 100);
    s.outMaxPct = clampf(s.outMaxPct, 5, 100);
    if (s.ssrWindowMs < 500) s.ssrWindowMs = 500;
    if (s.ssrWindowMs > 20000) s.ssrWindowMs = 20000;
    s.tuneHystC = clampf(s.tuneHystC, 0.1f, 5);
    s.tuneOutHighPct = clampf(s.tuneOutHighPct, 10, 100);
    if (s.tuneRule > 2) s.tuneRule = 1;
    s.runawayWindowMin = clampf(s.runawayWindowMin, 0, 240);
    s.runawayMinRiseC = clampf(s.runawayMinRiseC, 0.1f, 50);
    s.runawayMinOutPct = clampf(s.runawayMinOutPct, 10, 100);
    s.idleRiseC = clampf(s.idleRiseC, 0, 50);
    if (s.sensorFailLimit < 2) s.sensorFailLimit = 2;
    if (s.sensorFailLimit > 60) s.sensorFailLimit = 60;
    s.heaterSensorRequired = s.heaterSensorRequired ? 1 : 0;
    s.autoResume = s.autoResume ? 1 : 0;
    s.wasRunning = s.wasRunning ? 1 : 0;
    return memcmp(&o, &s, sizeof(s)) != 0;
}

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Idle: return "STOP";
        case Mode::Run: return "HEAT";
        case Mode::Tune: return "TUNE";
        case Mode::Fault: return "FAULT";
    }
    return "?";
}

void Controller::begin(const ControlSettings& s, uint32_t nowMs) {
    s_ = s;
    sanitize(s_);
    applySettings(s_);
    pid_.reset(0, 0);
    safety_.reset(nowMs);
    st_ = ControllerStatus();
    st_.targetC = s_.targetC;
    st_.modeSinceMs = nowMs;
    lastStepMs_ = nowMs;
}

void Controller::applySettings(const ControlSettings& in) {
    ControlSettings s = in;
    s.wasRunning = s_.wasRunning;  // служебное поле ведёт сам контроллер
    sanitize(s);
    s_ = s;
    PidGains g;
    g.kp = s_.kp;
    g.ki = s_.ki;
    g.kd = s_.kd;
    pid_.setGains(g);
    pid_.setOutputLimits(0, s_.outMaxPct);
    pid_.setDerivativeFilter(s_.dFilterSec);
    ramp_.setRate(s_.rampCpm);
    st_.targetC = s_.targetC;
}

void Controller::setTarget(float c) {
    s_.targetC = clampf(c, s_.spMinC, s_.spMaxC);
    st_.targetC = s_.targetC;
    atTempEventSent_ = false;
}

void Controller::setMode(Mode m, uint32_t nowMs) {
    if (st_.mode == m) return;
    st_.mode = m;
    st_.modeSinceMs = nowMs;
    const uint8_t running = (m == Mode::Run) ? 1 : 0;
    if (s_.wasRunning != running && m != Mode::Tune) {
        s_.wasRunning = running;
        dirty_ = true;
    }
}

bool Controller::start(uint32_t nowMs) {
    if (st_.mode == Mode::Fault) return false;
    if (st_.mode == Mode::Run) return true;
    const float pv = st_.meas.procValid ? st_.meas.procC : 0;
    ramp_.reset(pv < s_.targetC ? pv : s_.targetC);
    pid_.reset(pv, 0);
    safety_.reset(nowMs);
    tune_.abort();
    atTempEventSent_ = false;
    st_.atTemp = false;
    setMode(Mode::Run, nowMs);
    return true;
}

void Controller::stop(uint32_t nowMs) {
    if (st_.mode == Mode::Fault) return;  // из аварии выходим только через ack()
    tune_.abort();
    st_.outPct = 0;
    safety_.reset(nowMs);
    setMode(Mode::Idle, nowMs);
}

bool Controller::startTune(uint32_t nowMs) {
    if (st_.mode == Mode::Fault || !st_.meas.procValid) return false;
    RelayAutotune::Config c;
    c.setpoint = s_.targetC;
    c.hysteresis = s_.tuneHystC;
    c.outHigh = s_.tuneOutHighPct < s_.outMaxPct ? s_.tuneOutHighPct : s_.outMaxPct;
    c.outLow = 0;
    c.rule = (TuneRule)s_.tuneRule;
    tune_.start(c, st_.meas.procC, nowMs);
    safety_.reset(nowMs);
    setMode(Mode::Tune, nowMs);
    return true;
}

void Controller::raiseFault(Fault f, uint32_t nowMs) { enterFault(f, nowMs); }

void Controller::enterFault(Fault f, uint32_t nowMs) {
    tune_.abort();
    st_.fault = f;
    st_.outPct = 0;
    events_ |= EV_FAULT;
    setMode(Mode::Fault, nowMs);
}

bool Controller::ack(uint32_t nowMs) {
    if (st_.mode != Mode::Fault) return true;
    // Не даём сбросить аварию, пока её причина присутствует
    SafetyMonitor probe;
    probe.reset(nowMs);
    SafetyInputs in = safetyInputs(st_.meas);
    in.heatingEnabled = false;
    if (probe.check(in, s_, nowMs) != Fault::None) return false;
    st_.fault = Fault::None;
    safety_.reset(nowMs);
    st_.mode = Mode::Idle;  // напрямую: wasRunning уже сброшен
    st_.modeSinceMs = nowMs;
    return true;
}

SafetyInputs Controller::safetyInputs(const Measurement& m) const {
    SafetyInputs in;
    in.procValid = m.procValid;
    in.procC = m.procC;
    in.procFails = m.procFails;
    in.heaterPresent = m.heaterPresent;
    in.heaterValid = m.heaterValid;
    in.heaterC = m.heaterC;
    in.heaterFails = m.heaterFails;
    in.outputPct = st_.outPct;
    in.targetC = s_.targetC;
    in.heatingEnabled = st_.mode == Mode::Run || st_.mode == Mode::Tune;
    return in;
}

float Controller::step(const Measurement& m, uint32_t nowMs) {
    float dt = havePrev_ ? (nowMs - lastStepMs_) / 1000.0f : 1.0f;
    havePrev_ = true;
    lastStepMs_ = nowMs;
    if (dt > 10) dt = 10;  // защита от больших пропусков
    st_.meas = m;

    if (st_.mode != Mode::Fault) {
        const Fault f = safety_.check(safetyInputs(m), s_, nowMs);
        if (f != Fault::None) enterFault(f, nowMs);
    }

    float out = 0;
    switch (st_.mode) {
        case Mode::Idle:
        case Mode::Fault:
            out = 0;
            st_.spC = s_.targetC;
            break;

        case Mode::Run:
            if (!m.procValid) {
                out = 0;  // разовый сбой датчика — не греем «вслепую»
                break;
            }
            st_.spC = ramp_.update(s_.targetC, dt, m.procC, s_.rampHoldC);
            // Зона интегрирования: далеко от цели интеграл не копим —
            // иначе за долгий разогрев/плавление он «набегает» и даёт перерегулирование.
            pid_.freezeIntegral(s_.iZoneC > 0 && fabsf(s_.targetC - m.procC) > s_.iZoneC);
            out = pid_.update(st_.spC, m.procC, dt);
            st_.atTemp = fabsf(m.procC - s_.targetC) < 1.0f;
            if (st_.atTemp && !atTempEventSent_) {
                atTempEventSent_ = true;
                events_ |= EV_AT_TEMP;
            }
            break;

        case Mode::Tune:
            if (!m.procValid) {
                out = 0;
                break;
            }
            st_.spC = s_.targetC;
            out = tune_.update(m.procC, nowMs);
            st_.tuneCycles = tune_.cycles();
            if (tune_.state() == RelayAutotune::State::Done) {
                const PidGains g = tune_.result();
                st_.ku = tune_.ku();
                st_.tu = tune_.tuSec();
                s_.kp = g.kp;
                s_.ki = g.ki;
                s_.kd = g.kd;
                applySettings(s_);
                dirty_ = true;
                events_ |= EV_TUNE_DONE;
                // Сразу переходим в режим регулирования с новыми коэффициентами
                st_.mode = Mode::Idle;
                start(nowMs);
                out = 0;
            } else if (tune_.state() == RelayAutotune::State::Failed) {
                enterFault(Fault::TuneFailed, nowMs);
                out = 0;
            }
            break;
    }

    if (out < 0) out = 0;
    if (out > s_.outMaxPct) out = s_.outMaxPct;
    st_.outPct = out;
    st_.p = pid_.pTerm();
    st_.i = pid_.iTerm();
    st_.d = pid_.dTerm();
    st_.targetC = s_.targetC;
    return out;
}

}  // namespace ctl
