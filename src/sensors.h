#pragma once
#if SIM_SENSORS
#include "sim_sensors.h"
#else
// Два DS18B20 на одной шине 1-Wire: «парафин» (регулируемый) и «ТЭН» (защита, необязательный).
// Чтение неблокирующее: запрос преобразования → через 760 мс чтение.
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include "Controller.h"

class TempSensors {
public:
    explicit TempSensors(uint8_t pin) : ow_(pin), dt_(&ow_) {}

    // Адреса из настроек; нулевой адрес — «назначить автоматически»
    void begin(const uint8_t procAddr[8], const uint8_t heaterAddr[8]);
    void tick(uint32_t nowMs);

    bool takeNew() { bool n = new_; new_ = false; return n; }
    const ctl::Measurement& measurement() const { return m_; }

    void swapRoles();
    void rescan() { rescanReq_ = true; }
    uint8_t deviceCount() const { return count_; }
    bool procPresent() const { return procOk_; }
    bool heaterPresent() const { return heaterOk_; }
    const uint8_t* procAddr() const { return proc_; }
    const uint8_t* heaterAddr() const { return heater_; }
    static void addrToStr(const uint8_t a[8], char out[17]);

private:
    void discover();
    bool readOne(const uint8_t addr[8], float last, bool lastValid, float& out);

    OneWire ow_;
    DallasTemperature dt_;
    uint8_t proc_[8] = {}, heater_[8] = {};
    bool procOk_ = false, heaterOk_ = false;
    uint8_t count_ = 0;
    bool converting_ = false;
    uint32_t reqMs_ = 0, lastScanMs_ = 0;
    bool new_ = false;
    volatile bool rescanReq_ = false;
    ctl::Measurement m_;
};
#endif  // SIM_SENSORS
