#pragma once
// Активный зуммер (генератор внутри), неблокирующие сигналы.
#include <Arduino.h>

class Buzzer {
public:
    void begin(int pin) {
        pin_ = pin;
        if (pin_ >= 0) {
            pinMode(pin_, OUTPUT);
            digitalWrite(pin_, LOW);
        }
    }
    void setEnabled(bool e) { enabled_ = e; if (!e) set(false); }

    void beep(uint8_t count, uint16_t onMs = 80, uint16_t offMs = 120) {
        if (!enabled_ || pin_ < 0) return;
        count_ = count;
        onMs_ = onMs;
        offMs_ = offMs;
        t0_ = millis();
        phaseOn_ = true;
        set(true);
    }
    void alarm(bool on) { alarm_ = on; }

    void tick(uint32_t now) {
        if (pin_ < 0) return;
        if (alarm_ && enabled_ && count_ == 0) {  // авария: 3 коротких раз в 2 с
            const uint32_t ph = now % 2000;
            set(ph < 600 && (ph % 200) < 100);
            return;
        }
        if (count_ == 0) {
            if (on_) set(false);
            return;
        }
        const uint32_t el = now - t0_;
        if (phaseOn_ && el >= onMs_) {
            set(false);
            phaseOn_ = false;
            t0_ = now;
            if (--count_ == 0) return;
        } else if (!phaseOn_ && el >= offMs_) {
            set(true);
            phaseOn_ = true;
            t0_ = now;
        }
    }

private:
    void set(bool on) {
        on_ = on;
        if (pin_ >= 0) digitalWrite(pin_, on ? HIGH : LOW);
    }
    int pin_ = -1;
    bool enabled_ = true, alarm_ = false, on_ = false, phaseOn_ = false;
    uint8_t count_ = 0;
    uint16_t onMs_ = 80, offMs_ = 120;
    uint32_t t0_ = 0;
};
