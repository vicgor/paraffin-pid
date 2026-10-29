#pragma once
// Настройки регулятора (POD — сохраняются во флеш как есть).
#include <stdint.h>

namespace ctl {

// Верхние пределы НАСТРОЙКИ аварийных температур (значения по умолчанию ниже не меняются).
// Датчик DS18B20 физически не работает выше 125 °C (чтение отвергается в sensors.cpp),
// поэтому пределы выше 125 °C имеют смысл только с высокотемпературным датчиком (PT100)
// и обязательным аппаратным NC-термовыключателем KSD301/KSD9700 последовательно с ТЭНом.
constexpr float kMaxProcessLimitC = 150.0f;
constexpr float kMaxHeaterLimitC = 200.0f;

struct ControlSettings {
    // Уставка
    float targetC = 70.0f;     // целевая температура парафина
    float spMinC = 20.0f;
    float spMaxC = 95.0f;      // жёсткий верхний предел уставки
    float rampCpm = 2.0f;      // скорость роста уставки, °C/мин (0 — выкл.)
    float rampHoldC = 4.0f;    // рампа не уходит выше PV больше чем на N °C (0 — выкл.)

    // ПИД
    float kp = 10.0f;
    float ki = 0.02f;
    float kd = 100.0f;
    float dFilterSec = 10.0f;
    float iZoneC = 5.0f;       // интеграл копится только при |цель − T| < N °C (0 — всегда)
    float outMaxPct = 100.0f;  // ограничение мощности
    uint16_t ssrWindowMs = 2000;  // период ШИМ твердотельного реле

    // Автотюнинг
    float tuneHystC = 0.5f;
    float tuneOutHighPct = 100.0f;
    uint8_t tuneRule = 1;  // ctl::TuneRule (1 = Tyreus-Luyben)

    // Защиты
    float maxProcessC = 105.0f;  // аварийная T парафина (всегда активна)
    float maxHeaterC = 120.0f;   // аварийная T на ТЭНе (DS18B20 ≤ 125 °C! выше — только с PT100)
    uint8_t heaterSensorRequired = 0;  // 1 — без датчика ТЭНа работа запрещена
    float runawayWindowMin = 30.0f;    // окно проверки «нагрев есть, роста нет» (0 — выкл.)
    float runawayMinRiseC = 1.0f;
    float runawayMinOutPct = 25.0f;  // средняя мощность за окно, выше которой ждём рост
    float idleRiseC = 8.0f;  // рост T при выключенном нагреве → залипло SSR (0 — выкл.)
    uint16_t sensorFailLimit = 5;  // сколько подряд неудачных чтений = авария

    uint8_t autoResume = 0;  // продолжить нагрев после перезагрузки (не рекомендуется)
    uint8_t wasRunning = 0;  // служебное
};

// Приведение значений к допустимым диапазонам. Возвращает true, если что-то поправили.
bool sanitize(ControlSettings& s);

}  // namespace ctl
