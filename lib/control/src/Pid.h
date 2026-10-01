#pragma once
// ПИД-регулятор для медленных тепловых объектов.
//  - параллельная форма: u = Kp*e + I + D
//  - интегратор хранится в единицах выхода (%), поэтому смена Ki «на лету» безударная
//  - D-составляющая по измерению (нет «пинка» при смене уставки) + фильтр 1-го порядка
//  - анти-windup: условное интегрирование + ограничение интегратора пределами выхода
// Платформонезависимый код, без Arduino.

namespace ctl {

struct PidGains {
    float kp = 0;  // %/°C
    float ki = 0;  // %/(°C·с)
    float kd = 0;  // %·с/°C
};

class Pid {
public:
    void setGains(const PidGains& g) { g_ = g; }
    const PidGains& gains() const { return g_; }

    void setOutputLimits(float mn, float mx) {
        if (mn > mx) { float t = mn; mn = mx; mx = t; }
        outMin_ = mn;
        outMax_ = mx;
        i_ = clamp(i_);
    }

    // Постоянная времени фильтра производной, с (0 — без фильтра)
    void setDerivativeFilter(float tauSec) { dTau_ = tauSec < 0 ? 0 : tauSec; }

    // Сброс. initialOutput — желаемый выход в момент старта (безударное включение).
    void reset(float pv, float initialOutput = 0) {
        lastPv_ = pv;
        d_ = 0;
        i_ = clamp(initialOutput);
        p_ = 0;
        out_ = i_;
        primed_ = true;
    }

    // dt — секунды с прошлого вызова
    float update(float sp, float pv, float dt) {
        if (!primed_) reset(pv);
        if (dt <= 0) return out_;

        const float e = sp - pv;
        p_ = g_.kp * e;

        // Производная по измерению с фильтром
        const float dRaw = -g_.kd * (pv - lastPv_) / dt;
        const float alpha = (dTau_ > 0) ? dt / (dTau_ + dt) : 1.0f;
        d_ += alpha * (dRaw - d_);
        lastPv_ = pv;

        // Условное интегрирование: не копим интеграл, если выход уже в насыщении
        // и ошибка толкает его ещё дальше.
        const float iCand = i_ + g_.ki * e * dt;
        const float uCand = p_ + iCand + d_;
        const bool satHigh = uCand > outMax_ && e > 0;
        const bool satLow = uCand < outMin_ && e < 0;
        if (!satHigh && !satLow && !freezeI_) i_ = iCand;
        i_ = clamp(i_);

        out_ = clamp(p_ + i_ + d_);
        return out_;
    }

    // Заморозка интегратора (зона интегрирования задаётся снаружи)
    void freezeIntegral(bool f) { freezeI_ = f; }

    float output() const { return out_; }
    float pTerm() const { return p_; }
    float iTerm() const { return i_; }
    float dTerm() const { return d_; }

private:
    float clamp(float v) const { return v < outMin_ ? outMin_ : (v > outMax_ ? outMax_ : v); }

    PidGains g_;
    float outMin_ = 0, outMax_ = 100;
    float dTau_ = 0;
    float lastPv_ = 0;
    float p_ = 0, i_ = 0, d_ = 0, out_ = 0;
    bool primed_ = false;
    bool freezeI_ = false;
};

}  // namespace ctl
