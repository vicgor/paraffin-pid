#pragma once
// Симулятор двух DS18B20 для отладки без железа (сборка с -DSIM_SENSORS=1).
// Тот же интерфейс, что у TempSensors; температура считается по модели
// «ТЭН → парафин → среда» с плато плавления (как в test/test_control).
// Вход модели — фактическое состояние SSR (setHeater), поэтому PID,
// автотюнинг, защиты и UI работают как на реальной ванне.
#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "Controller.h"

class TempSensors {
public:
    explicit TempSensors(uint8_t) {}

    void begin(const uint8_t[8], const uint8_t[8]) { lastMs_ = millis(); }

    void setHeater(bool on) { heaterOn_ = on; }

    void tick(uint32_t nowMs) {
        // интегрируем модель с шагом 10 мс, «показания» — раз в секунду
        while (nowMs - lastMs_ >= kDtMs) {
            lastMs_ += kDtMs;
            step(heaterOn_ ? 1.0f : 0.0f, kDtMs / 1000.0f);
        }
        if (nowMs - lastReadMs_ < 1000) return;
        lastReadMs_ = nowMs;
        m_.procValid = !failProc;
        m_.procC = q(failProc ? 0 : waxC);
        m_.procFails = failProc ? (m_.procFails < 60000 ? m_.procFails + 1 : 60000) : 0;
        m_.heaterPresent = true;
        m_.heaterValid = true;
        m_.heaterC = q(heaterC);
        m_.heaterFails = 0;
        new_ = true;
    }

    bool takeNew() { bool n = new_; new_ = false; return n; }
    const ctl::Measurement& measurement() const { return m_; }

    void swapRoles() {}
    void rescan() {}
    uint8_t deviceCount() const { return 2; }
    bool procPresent() const { return true; }
    bool heaterPresent() const { return true; }
    const uint8_t* procAddr() const { return kProc_; }
    const uint8_t* heaterAddr() const { return kHeater_; }
    static void addrToStr(const uint8_t a[8], char out[17]) {
        static const char* hex = "0123456789ABCDEF";
        for (int k = 0; k < 8; ++k) {
            out[2 * k] = hex[a[k] >> 4];
            out[2 * k + 1] = hex[a[k] & 15];
        }
        out[16] = 0;
    }

    // --- параметры модели (меняются из CLI: sim ...) ---
    float ambient = 22, heaterC = 22, waxC = 22;
    float powerW = 1000, cHeater = 400, massKg = 3, cpWax = 2500;
    float gHW = 25, gWA = 3, meltC = 58, latentJ = 3 * 200e3f, melted = 0;
    float speed = 1;        // ускорение времени (1 = реальное)
    bool failProc = false;  // имитация обрыва датчика парафина

    void reset() { heaterC = waxC = ambient; melted = 0; }

private:
    static constexpr uint32_t kDtMs = 10;
    const uint8_t kProc_[8] = {0x28, 0x51, 0x4D, 0, 0, 0, 0, 0x01};
    const uint8_t kHeater_[8] = {0x28, 0x51, 0x4D, 0, 0, 0, 0, 0x02};

    static float q(float t) { return roundf(t * 16) / 16; }  // дискретность 0.0625 °C

    void step(float duty, float dt) {
        dt *= speed;
        const float qHW = gHW * (heaterC - waxC);
        heaterC += (powerW * duty - qHW) * dt / cHeater;
        float qNet = qHW - gWA * (waxC - ambient);
        if (waxC >= meltC && melted < latentJ && qNet > 0) {
            melted += qNet * dt;
            if (melted <= latentJ) return;
            qNet = (melted - latentJ) / dt;
            melted = latentJ;
        }
        waxC += qNet * dt / (massKg * cpWax);
    }

    bool heaterOn_ = false, new_ = false;
    uint32_t lastMs_ = 0, lastReadMs_ = 0;
    ctl::Measurement m_;
};
