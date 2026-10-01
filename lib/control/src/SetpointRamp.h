#pragma once
// Ограничение скорости роста уставки (мягкий разогрев).
// Важно для парафина: пока он твёрдый, теплопередача плохая и у ТЭНа
// возможен локальный перегрев. Рампа вверх ограничена, вниз — мгновенно
// (охлаждение всё равно пассивное).

namespace ctl {

class SetpointRamp {
public:
    void reset(float value) { v_ = value; }
    void setRate(float degPerMin) { rate_ = degPerMin < 0 ? 0 : degPerMin; }

    // pv/holdBand: «гарантированная рампа» — уставка не убегает от реальной
    // температуры дальше чем на holdBand °C (например, на плато плавления),
    // иначе после плато будет перерегулирование.
    float update(float target, float dtSec, float pv = -1e9f, float holdBand = 0) {
        if (rate_ <= 0 || target <= v_) {
            v_ = target;
        } else {
            const float step = rate_ * dtSec / 60.0f;
            v_ = (target - v_ > step) ? v_ + step : target;
            if (holdBand > 0 && v_ > pv + holdBand) v_ = (pv + holdBand > target) ? target : pv + holdBand;
        }
        return v_;
    }

    float value() const { return v_; }
    bool done(float target) const { return v_ >= target; }

private:
    float v_ = 0;
    float rate_ = 0;
};

}  // namespace ctl
