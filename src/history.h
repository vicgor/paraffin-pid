#pragma once
// Кольцевой буфер истории температуры (для графика на дисплее и в вебе).
#include <stdint.h>

class History {
public:
    static constexpr uint16_t kSize = 360;          // 360 × 5 с = 30 минут
    static constexpr uint32_t kIntervalMs = 5000;
    static constexpr int16_t kInvalid = INT16_MIN;

    void push(bool valid, float pv, float sp) {
        pv_[head_] = valid ? (int16_t)(pv * 10.0f) : kInvalid;
        sp_[head_] = (int16_t)(sp * 10.0f);
        head_ = (head_ + 1) % kSize;
        if (count_ < kSize) count_++;
    }
    uint16_t count() const { return count_; }
    // i = 0 — самая старая точка
    int16_t pv10(uint16_t i) const { return pv_[idx(i)]; }
    int16_t sp10(uint16_t i) const { return sp_[idx(i)]; }

private:
    uint16_t idx(uint16_t i) const { return (head_ + kSize - count_ + i) % kSize; }
    int16_t pv_[kSize] = {};
    int16_t sp_[kSize] = {};
    uint16_t head_ = 0, count_ = 0;
};

extern History g_history;
