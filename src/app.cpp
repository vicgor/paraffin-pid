#include "app.h"

#include "hal.h"
#include "pins.h"
#include "sensors.h"
#include "ssr.h"

namespace {

#if SIM_SENSORS
constexpr bool kSimSensors = true;
#else
constexpr bool kSimSensors = false;
#endif

hal::Mutex g_lock;
ctl::Controller g_ctl;
TempSensors g_sensors(PIN_ONEWIRE);
SsrOutput g_ssr;
AppSettings g_cfg;  // владелец — loop(); ctl-часть синхронизируется при сохранении

volatile bool g_swapReq = false, g_rescanReq = false;
volatile bool g_ssrOn = false;
volatile bool g_ctlDirty = false;  // контроллер сам изменил настройки (тюнинг)
bool g_pendingAutoStart = false;
bool g_wdtFault = false;
uint8_t g_events = 0;
uint32_t g_saveAtMs = 0;
bool g_savePending = false;
char g_id[9];

uint32_t g_lastMeasMs = 0;

// ------------------------------------------------- цикл управления (высокий приоритет)
void controlTick() {
    static bool first = true;
    if (first) {
        first = false;
        hal::watchdogBegin(5000);
    }
    hal::watchdogFeed();
    const uint32_t now = millis();

    if (g_swapReq) {
        g_swapReq = false;
        hal::LockGuard lk(g_lock);
        g_sensors.swapRoles();
    }
    if (g_rescanReq) {
        g_rescanReq = false;
        g_sensors.rescan();
    }
#if SIM_SENSORS
    g_sensors.setHeater(g_ssrOn);
#endif
    g_sensors.tick(now);

    float duty;
    uint16_t window;
    {
        hal::LockGuard lk(g_lock);
        if (g_sensors.takeNew()) {
            const ctl::Measurement& m = g_sensors.measurement();
            if (g_pendingAutoStart && m.procValid) {
                g_pendingAutoStart = false;
                g_ctl.step(m, now);
                g_ctl.start(now);
            }
            g_ctl.step(m, now);
            g_events |= g_ctl.takeEvents();
            if (g_ctl.takeSettingsDirty()) g_ctlDirty = true;
            g_lastMeasMs = now;
        }
        // Страховка: измерения перестали приходить (зависла шина) — регулятор
        // не получает step(), значит и аварию не увидит. Выключаем нагрев сами.
        if (now - g_lastMeasMs > 5000) {
            ctl::Measurement m = g_sensors.measurement();
            m.procValid = false;
            m.procFails = 60000;
            g_ctl.step(m, now);
            g_lastMeasMs = now;
        }
        const ctl::Mode mode = g_ctl.status().mode;
        duty = (mode == ctl::Mode::Run || mode == ctl::Mode::Tune) ? g_ctl.output() : 0;
        window = g_ctl.settings().ssrWindowMs;
    }
    g_ssr.setWindow(window);
    g_ssr.setDuty(duty);
    g_ssr.tick(now);
    g_ssrOn = g_ssr.isOn();
}

}  // namespace

#if SIM_SENSORS
TempSensors& simSensors() { return g_sensors; }
#endif

namespace app {

const char* deviceId() { return g_id; }

void begin() {
    // Первым делом — гарантированно выключить нагрев.
    // В sim-сборке выход SSR только логический: пин не активируется никогда.
    g_ssr.begin(PIN_SSR, SSR_ACTIVE_HIGH, kSimSensors);
    if (kSimSensors) Serial.println("[sim] SIM_SENSORS: SSR pin disabled (dry run)");
    snprintf(g_id, sizeof(g_id), "%08lx", (unsigned long)hal::chipId());

    const bool loaded = settings_store::load(g_cfg);
    Serial.printf("[cfg] %s\n", loaded ? "loaded" : "defaults");

    g_wdtFault = hal::resetWasWatchdog();
    const uint32_t now = millis();
    g_ctl.begin(g_cfg.ctl, now);
    if (g_wdtFault) {
        g_ctl.raiseFault(ctl::Fault::WatchdogReset, now);
    } else if (g_cfg.ctl.autoResume && g_cfg.ctl.wasRunning) {
        g_pendingAutoStart = true;
    }

    g_sensors.begin(g_cfg.procAddr, g_cfg.heaterAddr);
    Serial.printf("[ds18b20] found %u (proc=%d heater=%d)\n", g_sensors.deviceCount(), g_sensors.procPresent(),
                  g_sensors.heaterPresent());
    // Сохранить автоназначенные адреса
    if (memcmp(g_cfg.procAddr, g_sensors.procAddr(), 8) || memcmp(g_cfg.heaterAddr, g_sensors.heaterAddr(), 8))
        requestSave();

    hal::startControlLoop(controlTick, 5);
}

void loopTick() {
    if (g_ctlDirty) {
        g_ctlDirty = false;
        requestSave();
    }
    if (g_savePending && (int32_t)(millis() - g_saveAtMs) >= 0) saveNow();
}

ctl::ControllerStatus status() {
    hal::LockGuard lk(g_lock);
    return g_ctl.status();
}

ctl::ControlSettings controlSettings() {
    hal::LockGuard lk(g_lock);
    return g_ctl.settings();
}

void applyControlSettings(const ctl::ControlSettings& s) {
    {
        hal::LockGuard lk(g_lock);
        g_ctl.applySettings(s);
    }
    requestSave();
}

bool heaterOn() { return g_ssrOn; }

bool start() {
    hal::LockGuard lk(g_lock);
    return g_ctl.start(millis());
}
void stop() {
    hal::LockGuard lk(g_lock);
    g_pendingAutoStart = false;
    g_ctl.stop(millis());
}
bool startTune() {
    hal::LockGuard lk(g_lock);
    return g_ctl.startTune(millis());
}
bool ack() {
    hal::LockGuard lk(g_lock);
    return g_ctl.ack(millis());
}
void setTarget(float c) {
    {
        hal::LockGuard lk(g_lock);
        g_ctl.setTarget(c);
    }
    requestSave();
}
uint8_t takeEvents() {
    hal::LockGuard lk(g_lock);
    const uint8_t e = g_events;
    g_events = 0;
    return e;
}

SensorInfo sensorInfo() {
    SensorInfo i;
    hal::LockGuard lk(g_lock);  // адреса меняются только в controlTick, но читаем под замком
    i.count = g_sensors.deviceCount();
    i.procPresent = g_sensors.procPresent();
    i.heaterPresent = g_sensors.heaterPresent();
    TempSensors::addrToStr(g_sensors.procAddr(), i.procAddr);
    TempSensors::addrToStr(g_sensors.heaterAddr(), i.heaterAddr);
    return i;
}
void swapSensors() {
    g_swapReq = true;
    requestSave();
}
void rescanSensors() { g_rescanReq = true; }

AppSettings& settings() { return g_cfg; }

void requestSave() {
    g_savePending = true;
    g_saveAtMs = millis() + 3000;  // откладываем: энкодер крутят — не пишем флеш на каждый щелчок
}

void saveNow() {
    g_savePending = false;
    {
        hal::LockGuard lk(g_lock);
        g_cfg.ctl = g_ctl.settings();
        memcpy(g_cfg.procAddr, g_sensors.procAddr(), 8);
        memcpy(g_cfg.heaterAddr, g_sensors.heaterAddr(), 8);
    }
    const bool ok = settings_store::save(g_cfg);
    Serial.printf("[cfg] save %s\n", ok ? "ok" : "FAILED");
}

void factoryReset() {
    {
        hal::LockGuard lk(g_lock);
        g_ctl.stop(millis());
    }
    settings_store::factoryReset();
    delay(100);
    hal::reboot();
}

}  // namespace app
