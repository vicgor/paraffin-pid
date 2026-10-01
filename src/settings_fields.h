#pragma once
// Таблица полей ControlSettings: единый источник для CLI (set/show), веб-API и MQTT.
#include <stddef.h>
#include <string.h>

#include "ControlSettings.h"

enum class FieldType : uint8_t { F32, U8, U16 };

struct FieldDef {
    const char* key;
    FieldType type;
    size_t offset;
    const char* help;
};

#define CS_F(k, t, h) {#k, FieldType::t, offsetof(ctl::ControlSettings, k), h}

static const FieldDef kFields[] = {
    CS_F(targetC, F32, "уставка, °C"),
    CS_F(spMinC, F32, "мин. уставка, °C"),
    CS_F(spMaxC, F32, "макс. уставка, °C"),
    CS_F(rampCpm, F32, "скорость роста уставки, °C/мин (0 — выкл.)"),
    CS_F(rampHoldC, F32, "рампа не выше T+N °C (0 — выкл.)"),
    CS_F(kp, F32, "Kp, %/°C"),
    CS_F(ki, F32, "Ki, %/(°C·с)"),
    CS_F(kd, F32, "Kd, %·с/°C"),
    CS_F(dFilterSec, F32, "фильтр D, с"),
    CS_F(iZoneC, F32, "зона интегрирования, °C (0 — всегда)"),
    CS_F(outMaxPct, F32, "ограничение мощности, %"),
    CS_F(ssrWindowMs, U16, "период ШИМ SSR, мс"),
    CS_F(tuneHystC, F32, "гистерезис автотюнинга, °C"),
    CS_F(tuneOutHighPct, F32, "мощность при автотюнинге, %"),
    CS_F(tuneRule, U8, "правило: 0 ZN, 1 Tyreus-Luyben, 2 без перерегулирования"),
    CS_F(maxProcessC, F32, "аварийная T парафина, °C"),
    CS_F(maxHeaterC, F32, "аварийная T ТЭНа, °C"),
    CS_F(heaterSensorRequired, U8, "датчик ТЭНа обязателен (0/1)"),
    CS_F(runawayWindowMin, F32, "окно контроля роста T, мин (0 — выкл.)"),
    CS_F(runawayMinRiseC, F32, "мин. рост T за окно, °C"),
    CS_F(runawayMinOutPct, F32, "средняя мощность для контроля роста, %"),
    CS_F(idleRiseC, F32, "рост T при выкл. нагреве → авария, °C (0 — выкл.)"),
    CS_F(sensorFailLimit, U16, "ошибок чтения подряд до аварии"),
    CS_F(autoResume, U8, "продолжить нагрев после перезагрузки (0/1)"),
};
#undef CS_F

static const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

inline const FieldDef* findField(const char* key) {
    for (size_t k = 0; k < kFieldCount; ++k)
        if (strcmp(kFields[k].key, key) == 0) return &kFields[k];
    return nullptr;
}

inline float fieldGet(const ctl::ControlSettings& s, const FieldDef& f) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s) + f.offset;
    switch (f.type) {
        case FieldType::F32: { float v; memcpy(&v, p, 4); return v; }
        case FieldType::U8: return *p;
        case FieldType::U16: { uint16_t v; memcpy(&v, p, 2); return v; }
    }
    return 0;
}

inline void fieldSet(ctl::ControlSettings& s, const FieldDef& f, float v) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&s) + f.offset;
    switch (f.type) {
        case FieldType::F32: memcpy(p, &v, 4); break;
        case FieldType::U8: *p = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); break;
        case FieldType::U16: {
            uint16_t u = (uint16_t)(v < 0 ? 0 : (v > 65535 ? 65535 : v));
            memcpy(p, &u, 2);
            break;
        }
    }
}
