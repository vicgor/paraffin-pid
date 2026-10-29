// Нативные тесты логики регулятора на модели «ТЭН + ёмкость с парафином».
// Запуск: pio test -e native
#include <math.h>
#include <stdio.h>
#include <unity.h>

#include "Autotune.h"
#include "Controller.h"
#include "Pid.h"
#include "SetpointRamp.h"

using namespace ctl;

// ---------------- Модель объекта ----------------
// Двухъёмкостная модель: ТЭН (малая теплоёмкость) → парафин (большая) → среда.
// Учитывается скрытая теплота плавления (плато при Tmelt).
struct WaxPot {
    float ambient = 22;
    float heaterC = 22, waxC = 22;
    float powerW = 1000;
    float cHeater = 400;           // Дж/K
    float massKg = 3, cpWax = 2500;  // Дж/(кг·K)
    float gHW = 25;                // Вт/K ТЭН→парафин
    float gWA = 3;                 // Вт/K парафин→среда
    float meltC = 58, latentJ = 3 * 200e3f, melted = 0;  // Дж уже «вложено» в плавление
    bool sensorInWax = true;

    void step(float dutyPct, float dt) {
        const float pIn = powerW * dutyPct / 100.0f;
        const float qHW = gHW * (heaterC - waxC);
        heaterC += (pIn - qHW) * dt / cHeater;
        float qNet = qHW - gWA * (waxC - ambient);
        // плато плавления
        if (waxC >= meltC && melted < latentJ && qNet > 0) {
            melted += qNet * dt;
            if (melted > latentJ) {
                qNet = melted - latentJ;
                melted = latentJ;
                waxC += qNet / (massKg * cpWax);
            }
            return;
        }
        waxC += qNet * dt / (massKg * cpWax);
    }
    float sensor() const { return sensorInWax ? waxC : ambient; }
};

static Measurement meas(const WaxPot& p, bool heater = true) {
    Measurement m;
    m.procValid = true;
    m.procC = roundf(p.sensor() * 16) / 16;  // дискретность DS18B20 0.0625 °C
    m.heaterPresent = heater;
    m.heaterValid = heater;
    m.heaterC = roundf(p.heaterC * 16) / 16;
    return m;
}

// Прогон контроллера: шаг измерения 1 с, ШИМ усредняется (окно 2 с ≪ τ объекта)
struct SimResult {
    float maxC = -1e9f;
    float finalC = 0;
    float settleMin = -1;
};

static SimResult runLoop(Controller& c, WaxPot& p, uint32_t& t, uint32_t seconds, float target,
                         bool heater = true) {
    SimResult r;
    for (uint32_t k = 0; k < seconds; ++k) {
        const float u = c.step(meas(p, heater), t);
        for (int j = 0; j < 10; ++j) p.step(u, 0.1f);
        t += 1000;
        if (p.waxC > r.maxC) r.maxC = p.waxC;
        if (r.settleMin < 0 && fabsf(p.waxC - target) < 0.5f) r.settleMin = k / 60.0f;
    }
    r.finalC = p.waxC;
    return r;
}

// ---------------- Тесты ----------------

void test_pid_antiwindup() {
    Pid pid;
    pid.setGains({10, 0.5f, 0});
    pid.setOutputLimits(0, 100);
    pid.reset(20);
    for (int k = 0; k < 1000; ++k) pid.update(80, 20, 1);  // огромная ошибка долго
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100, pid.output());
    TEST_ASSERT_TRUE(pid.iTerm() <= 100.0f);
    // Как только PV превысил SP, выход должен быстро упасть (нет накопленного windup)
    pid.update(80, 81, 1);
    TEST_ASSERT_TRUE(pid.output() < 100.0f);
}

void test_pid_derivative_on_measurement() {
    Pid pid;
    pid.setGains({0, 0, 50});
    pid.setOutputLimits(-100, 100);
    pid.reset(50);
    // Скачок уставки не должен давать D-«пинок»
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, pid.update(90, 50, 1));
    // Рост PV даёт отрицательный D
    TEST_ASSERT_TRUE(pid.update(90, 51, 1) < 0);
}

void test_ramp() {
    SetpointRamp r;
    r.setRate(2);
    r.reset(20);
    for (int k = 0; k < 60; ++k) r.update(70, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22, r.value());
    r.update(10, 1);  // вниз — мгновенно
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10, r.value());
}

void test_gains_rules() {
    PidGains g = gainsFromUltimate(10, 100, TuneRule::ZieglerNichols);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 6, g.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 6 / 50.0f, g.ki);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 6 * 12.5f, g.kd);
}

void test_closed_loop_default_gains_with_melting() {
    WaxPot pot;
    Controller c;
    ControlSettings s;
    s.targetC = 75;
    uint32_t t = 0;
    c.begin(s, t);
    c.step(meas(pot), t);
    TEST_ASSERT_TRUE(c.start(t));
    SimResult r = runLoop(c, pot, t, 5 * 3600, 75);
    printf("  default gains: max=%.2f final=%.2f settle=%.0f min, fault=%s\n", r.maxC, r.finalC,
           r.settleMin, faultName(c.status().fault));
    TEST_ASSERT_EQUAL(Mode::Run, c.status().mode);  // плато плавления не вызвало NO RISE
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 75, r.finalC);
    TEST_ASSERT_TRUE(r.maxC < 75 + 3.0f);
}

void test_autotune_then_regulate() {
    WaxPot pot;
    pot.melted = pot.latentJ;  // парафин уже расплавлен (так и надо тюнить)
    pot.waxC = pot.heaterC = 65;
    Controller c;
    ControlSettings s;
    s.targetC = 75;
    s.tuneOutHighPct = 60;
    uint32_t t = 0;
    c.begin(s, t);
    c.step(meas(pot), t);
    TEST_ASSERT_TRUE(c.startTune(t));
    uint8_t ev = 0;
    for (int k = 0; k < 6 * 3600 && c.status().mode == Mode::Tune; ++k) {
        const float u = c.step(meas(pot), t);
        for (int j = 0; j < 10; ++j) pot.step(u, 0.1f);
        t += 1000;
        ev |= c.takeEvents();
    }
    printf("  autotune: Ku=%.2f Tu=%.0fs -> Kp=%.3f Ki=%.5f Kd=%.1f, cycles=%d\n", c.status().ku,
           c.status().tu, c.settings().kp, c.settings().ki, c.settings().kd, c.status().tuneCycles);
    TEST_ASSERT_TRUE(ev & EV_TUNE_DONE);
    TEST_ASSERT_EQUAL(Mode::Run, c.status().mode);
    TEST_ASSERT_TRUE(c.status().ku > 0 && c.status().tu > 0);
    TEST_ASSERT_TRUE(c.takeSettingsDirty());

    // Ступенька уставки 75 → 85 с новыми коэффициентами
    c.setTarget(85);
    SimResult r = runLoop(c, pot, t, 3 * 3600, 85);
    printf("  tuned step 75->85: max=%.2f final=%.2f settle=%.0f min\n", r.maxC, r.finalC, r.settleMin);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 85, r.finalC);
    TEST_ASSERT_TRUE(r.maxC < 85 + 3.0f);
}

void test_fault_overtemp_and_ack() {
    Controller c;
    ControlSettings s;
    uint32_t t = 0;
    c.begin(s, t);
    Measurement m;
    m.procValid = true;
    m.procC = 50;
    c.step(m, t);
    c.start(t);
    m.procC = s.maxProcessC + 1;
    c.step(m, t += 1000);
    TEST_ASSERT_EQUAL(Mode::Fault, c.status().mode);
    TEST_ASSERT_EQUAL(Fault::OverTempProcess, c.status().fault);
    TEST_ASSERT_EQUAL_FLOAT(0, c.output());
    TEST_ASSERT_FALSE(c.start(t));
    TEST_ASSERT_FALSE(c.ack(t));  // причина ещё есть
    m.procC = 60;
    c.step(m, t += 1000);
    TEST_ASSERT_TRUE(c.ack(t));
    TEST_ASSERT_EQUAL(Mode::Idle, c.status().mode);
}

void test_fault_sensor() {
    Controller c;
    ControlSettings s;
    uint32_t t = 0;
    c.begin(s, t);
    Measurement m;
    m.procValid = true;
    m.procC = 40;
    c.step(m, t);
    c.start(t);
    m.procValid = false;
    for (uint16_t k = 1; k <= s.sensorFailLimit; ++k) {
        m.procFails = k;
        float u = c.step(m, t += 1000);
        TEST_ASSERT_EQUAL_FLOAT(0, u);  // не греем вслепую
    }
    TEST_ASSERT_EQUAL(Fault::SensorProcess, c.status().fault);
}

void test_fault_no_heat_rise() {
    // Датчик выпал из ёмкости: показывает комнатную температуру
    WaxPot pot;
    pot.sensorInWax = false;
    Controller c;
    ControlSettings s;
    uint32_t t = 0;
    c.begin(s, t);
    c.step(meas(pot, false), t);
    c.start(t);
    runLoop(c, pot, t, (uint32_t)(s.runawayWindowMin * 60) * 3, 70, false);
    TEST_ASSERT_EQUAL(Fault::NoHeatRise, c.status().fault);
}

void test_fault_heater_overtemp() {
    WaxPot pot;
    pot.gHW = 2;  // ТЭН плохо отдаёт тепло (сухой ТЭН / парафин твёрдый)
    Controller c;
    ControlSettings s;
    uint32_t t = 0;
    c.begin(s, t);
    c.step(meas(pot), t);
    c.start(t);
    runLoop(c, pot, t, 3600, 70);
    TEST_ASSERT_EQUAL(Fault::OverTempHeater, c.status().fault);
    TEST_ASSERT_TRUE(pot.heaterC < s.maxHeaterC + 5);
}

void test_fault_ssr_stuck() {
    WaxPot pot;
    Controller c;
    ControlSettings s;
    uint32_t t = 0;
    c.begin(s, t);
    for (int k = 0; k < 3600 && c.status().mode != Mode::Fault; ++k) {
        c.step(meas(pot), t);  // контроллер в STOP...
        for (int j = 0; j < 10; ++j) pot.step(100, 0.1f);  // ...а ТЭН греет
        t += 1000;
    }
    TEST_ASSERT_EQUAL(Fault::IdleHeating, c.status().fault);
}

void test_sanitize() {
    ControlSettings s;
    s.targetC = 300;
    s.maxProcessC = 300;
    s.maxHeaterC = 350;
    s.kp = NAN;
    TEST_ASSERT_TRUE(sanitize(s));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, kMaxProcessLimitC, s.maxProcessC);  // потолок 150 °C
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, kMaxHeaterLimitC, s.maxHeaterC);    // потолок 200 °C
    TEST_ASSERT_TRUE(s.targetC <= s.spMaxC);
    TEST_ASSERT_TRUE(s.spMaxC < s.maxProcessC);
    TEST_ASSERT_FALSE(isnan(s.kp));

    // Значения по умолчанию не должны измениться и не должны поправляться
    ControlSettings d;
    TEST_ASSERT_FALSE(sanitize(d));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 105.0f, d.maxProcessC);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, d.maxHeaterC);

    // Нижняя граница и NaN/Inf в аварийных пределах → по умолчанию
    ControlSettings n;
    n.maxProcessC = INFINITY;
    n.maxHeaterC = NAN;
    sanitize(n);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 105.0f, n.maxProcessC);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, n.maxHeaterC);
    n.maxHeaterC = 5;
    sanitize(n);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.0f, n.maxHeaterC);
}

void test_heater_limit_above_ds18b20() {
    // Порог ТЭНа выше 125 °C (для PT100) должен сохраняться и срабатывать как HTR HOT
    ControlSettings s;
    s.maxHeaterC = 175;
    TEST_ASSERT_FALSE(sanitize(s));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 175.0f, s.maxHeaterC);

    Controller c;
    uint32_t t = 0;
    c.begin(s, t);
    Measurement m;
    m.procValid = true;
    m.procC = 70;
    m.heaterPresent = true;
    m.heaterValid = true;
    m.heaterC = 170;  // ниже порога — аварии нет
    c.step(m, t += 1000);
    TEST_ASSERT_EQUAL(Fault::None, c.status().fault);
    m.heaterC = 176;  // выше порога
    c.step(m, t += 1000);
    TEST_ASSERT_EQUAL(Mode::Fault, c.status().mode);
    TEST_ASSERT_EQUAL(Fault::OverTempHeater, c.status().fault);
    TEST_ASSERT_EQUAL_FLOAT(0, c.output());
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pid_antiwindup);
    RUN_TEST(test_pid_derivative_on_measurement);
    RUN_TEST(test_ramp);
    RUN_TEST(test_gains_rules);
    RUN_TEST(test_sanitize);
    RUN_TEST(test_heater_limit_above_ds18b20);
    RUN_TEST(test_closed_loop_default_gains_with_melting);
    RUN_TEST(test_autotune_then_regulate);
    RUN_TEST(test_fault_overtemp_and_ack);
    RUN_TEST(test_fault_sensor);
    RUN_TEST(test_fault_no_heat_rise);
    RUN_TEST(test_fault_heater_overtemp);
    RUN_TEST(test_fault_ssr_stuck);
    return UNITY_END();
}
