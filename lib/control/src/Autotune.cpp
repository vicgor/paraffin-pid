#include "Autotune.h"

#include <math.h>

namespace ctl {

const char* tuneRuleName(TuneRule r) {
    switch (r) {
        case TuneRule::ZieglerNichols: return "Ziegler-Nichols";
        case TuneRule::TyreusLuyben: return "Tyreus-Luyben";
        case TuneRule::NoOvershoot: return "No overshoot";
    }
    return "?";
}

PidGains gainsFromUltimate(float ku, float tu, TuneRule rule) {
    PidGains g;
    if (ku <= 0 || tu <= 0) return g;
    float kp, ti, td;
    switch (rule) {
        case TuneRule::ZieglerNichols: kp = 0.6f * ku; ti = 0.5f * tu; td = 0.125f * tu; break;
        case TuneRule::NoOvershoot: kp = 0.2f * ku; ti = 0.5f * tu; td = tu / 3.0f; break;
        case TuneRule::TyreusLuyben:
        default: kp = ku / 2.2f; ti = 2.2f * tu; td = tu / 6.3f; break;
    }
    g.kp = kp;
    g.ki = kp / ti;
    g.kd = kp * td;
    return g;
}

void RelayAutotune::start(const Config& cfg, float pv, uint32_t nowMs) {
    cfg_ = cfg;
    if (cfg_.cyclesToMeasure < 2) cfg_.cyclesToMeasure = 2;
    if (cfg_.cyclesToMeasure > kMaxStore - 1) cfg_.cyclesToMeasure = kMaxStore - 1;
    if (cfg_.maxCycles < cfg_.cyclesToMeasure + 1) cfg_.maxCycles = cfg_.cyclesToMeasure + 1;
    state_ = State::Running;
    t0_ = nowMs;
    high_ = pv < cfg_.setpoint;
    phaseMin_ = phaseMax_ = pv;
    haveRiseSwitch_ = false;
    haveMin_ = false;
    cycles_ = 0;
    ku_ = tu_ = 0;
    fail_ = "";
    out_ = high_ ? cfg_.outHigh : cfg_.outLow;
}

float RelayAutotune::update(float pv, uint32_t nowMs) {
    if (state_ != State::Running) {
        out_ = 0;
        return out_;
    }
    if ((nowMs - t0_) / 1000u > cfg_.timeoutSec) {
        state_ = State::Failed;
        fail_ = "timeout";
        out_ = 0;
        return out_;
    }

    const float sp = cfg_.setpoint, h = cfg_.hysteresis;
    if (high_) {
        if (pv < phaseMin_) phaseMin_ = pv;
        if (pv > sp + h) {  // high → low
            lastMin_ = phaseMin_;
            haveMin_ = true;
            high_ = false;
            phaseMax_ = pv;
        }
    } else {
        if (pv > phaseMax_) phaseMax_ = pv;
        if (pv < sp - h) {  // low → high: завершён полный цикл
            if (haveRiseSwitch_ && haveMin_) {
                const int idx = cycles_ % kMaxStore;
                periods_[idx] = (nowMs - lastRiseSwitch_) / 1000.0f;
                amps_[idx] = (phaseMax_ - lastMin_) / 2.0f;
                cycles_++;
            }
            lastRiseSwitch_ = nowMs;
            haveRiseSwitch_ = true;
            high_ = true;
            phaseMin_ = pv;

            const uint8_t n = cfg_.cyclesToMeasure;
            if (cycles_ >= n + 1) {  // первый записанный цикл — переходный
                float pMin = 1e9f, pMax = 0;
                for (int k = 0; k < n; ++k) {
                    const float p = periods_[(cycles_ - 1 - k) % kMaxStore];
                    if (p < pMin) pMin = p;
                    if (p > pMax) pMax = p;
                }
                if (pMax <= pMin * 1.3f || cycles_ >= cfg_.maxCycles) {
                    finish();
                    return out_;
                }
            }
        }
    }
    out_ = high_ ? cfg_.outHigh : cfg_.outLow;
    return out_;
}

void RelayAutotune::finish() {
    const uint8_t n = cfg_.cyclesToMeasure;
    float pSum = 0, aSum = 0;
    for (int k = 0; k < n; ++k) {
        const int idx = (cycles_ - 1 - k) % kMaxStore;
        pSum += periods_[idx];
        aSum += amps_[idx];
    }
    const float tu = pSum / n;
    const float a = aSum / n;
    const float h = cfg_.hysteresis;
    out_ = 0;
    if (a < 0.05f || tu <= 0) {
        state_ = State::Failed;
        fail_ = "no oscillation";
        return;
    }
    const float aEff = (a > h * 1.05f) ? sqrtf(a * a - h * h) : a;
    const float d = (cfg_.outHigh - cfg_.outLow) / 2.0f;
    ku_ = 4.0f * d / (3.14159265f * aEff);
    tu_ = tu;
    state_ = State::Done;
}

}  // namespace ctl
