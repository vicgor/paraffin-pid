#include "sensors.h"

#include <math.h>

namespace {
constexpr uint32_t kPeriodMs = 1000;
constexpr uint32_t kConvMs = 760;  // 12 бит: 750 мс
constexpr uint32_t kRescanMs = 10000;

bool isZero(const uint8_t a[8]) {
    for (int k = 0; k < 8; ++k)
        if (a[k]) return false;
    return true;
}
bool same(const uint8_t a[8], const uint8_t b[8]) { return memcmp(a, b, 8) == 0; }
}  // namespace

void TempSensors::addrToStr(const uint8_t a[8], char out[17]) {
    static const char* hex = "0123456789ABCDEF";
    for (int k = 0; k < 8; ++k) {
        out[2 * k] = hex[a[k] >> 4];
        out[2 * k + 1] = hex[a[k] & 15];
    }
    out[16] = 0;
}

void TempSensors::begin(const uint8_t procAddr[8], const uint8_t heaterAddr[8]) {
    memcpy(proc_, procAddr, 8);
    memcpy(heater_, heaterAddr, 8);
    dt_.begin();
    dt_.setWaitForConversion(false);
    dt_.setCheckForConversion(false);
    discover();
}

void TempSensors::discover() {
    uint8_t found[4][8];
    count_ = 0;
    ow_.reset_search();
    uint8_t a[8];
    while (count_ < 4 && ow_.search(a)) {
        if (OneWire::crc8(a, 7) != a[7]) continue;
        if (a[0] != 0x28 && a[0] != 0x10 && a[0] != 0x22) continue;  // DS18B20/DS18S20/DS1822
        memcpy(found[count_++], a, 8);
    }
    auto has = [&](const uint8_t x[8]) {
        for (int k = 0; k < count_; ++k)
            if (same(found[k], x)) return true;
        return false;
    };
    procOk_ = !isZero(proc_) && has(proc_);
    heaterOk_ = !isZero(heater_) && has(heater_) && !(procOk_ && same(proc_, heater_));

    // Автоназначение: первый свободный — парафин, второй — ТЭН.
    // Если сохранённый адрес не найден (датчик заменили) — тоже переназначаем.
    for (int k = 0; k < count_; ++k) {
        const bool used = (procOk_ && same(found[k], proc_)) || (heaterOk_ && same(found[k], heater_));
        if (used) continue;
        if (!procOk_) {
            memcpy(proc_, found[k], 8);
            procOk_ = true;
        } else if (!heaterOk_) {
            memcpy(heater_, found[k], 8);
            heaterOk_ = true;
        }
    }
    for (int k = 0; k < count_; ++k)
        if (dt_.getResolution(found[k]) != 12) dt_.setResolution(found[k], 12);
    lastScanMs_ = millis();
}

void TempSensors::swapRoles() {
    uint8_t t[8];
    memcpy(t, proc_, 8);
    memcpy(proc_, heater_, 8);
    memcpy(heater_, t, 8);
    const bool b = procOk_;
    procOk_ = heaterOk_;
    heaterOk_ = b;
}

bool TempSensors::readOne(const uint8_t addr[8], float last, bool lastValid, float& out) {
    const float t = dt_.getTempC(addr);
    if (t == DEVICE_DISCONNECTED_C || isnan(t) || t < -40 || t > 125) return false;
    // 85.0 — значение после сброса датчика (провал питания). Принимаем, только если
    // предыдущее чтение было рядом (парафин действительно может быть 85 °C).
    if (t == 85.0f && !(lastValid && fabsf(last - 85.0f) < 3.0f)) return false;
    // Отсечка импульсных помех: > 10 °C за секунду физически невозможно
    if (lastValid && fabsf(t - last) > 10.0f) return false;
    out = t;
    return true;
}

void TempSensors::tick(uint32_t nowMs) {
    if (rescanReq_ || ((!procOk_ || (count_ < 2)) && !converting_ && nowMs - lastScanMs_ > kRescanMs)) {
        rescanReq_ = false;
        discover();
    }
    if (!converting_) {
        if (nowMs - reqMs_ >= kPeriodMs) {
            dt_.requestTemperatures();  // всем датчикам сразу (Skip ROM)
            reqMs_ = nowMs;
            converting_ = true;
        }
        return;
    }
    if (nowMs - reqMs_ < kConvMs) return;
    converting_ = false;

    float v;
    if (procOk_ && readOne(proc_, m_.procC, m_.procValid, v)) {
        m_.procC = v;
        m_.procValid = true;
        m_.procFails = 0;
    } else {
        m_.procValid = false;
        if (m_.procFails < 60000) m_.procFails++;
    }

    m_.heaterPresent = heaterOk_;
    if (heaterOk_ && readOne(heater_, m_.heaterC, m_.heaterValid, v)) {
        m_.heaterC = v;
        m_.heaterValid = true;
        m_.heaterFails = 0;
    } else {
        m_.heaterValid = false;
        if (heaterOk_ && m_.heaterFails < 60000) m_.heaterFails++;
        if (!heaterOk_) m_.heaterFails = 0;
    }
    new_ = true;
}
