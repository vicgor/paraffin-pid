// ПИД-регулятор нагревателя для плавки парафина.
// RP2350 / ESP32-S3 / ESP32-C6, DS18B20, SSR, энкодер, OLED/TFT, Wi-Fi + MQTT.
//
// Архитектура:
//   [контекст управления, 5 мс] датчики → Controller (ПИД/тюнинг/защиты) → SSR, watchdog
//   [loop()]                    UI, веб, MQTT, консоль, зуммер, история, сохранение настроек
#include <Arduino.h>

#include "app.h"
#include "buzzer.h"
#include "cli.h"
#include "display/display.h"
#include "hal.h"
#include "history.h"
#include "input.h"
#include "net/net.h"
#include "pins.h"
#include "ui.h"

History g_history;

namespace {
Display* g_disp = nullptr;
InputDevice g_input;
Ui g_ui;
Buzzer g_buzzer;
uint32_t g_histAt = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);  // USB CDC: дать время подключиться
    Serial.printf("\n=== Paraffin PID %s on %s (%s) ===\n", FW_VERSION, hal::chipName(), APP_BOARD_NAME);

    app::begin();  // первым делом выключает SSR, затем стартует контур управления

    g_buzzer.begin(PIN_BUZZER);
    g_buzzer.setEnabled(app::settings().buzzer);
    g_input.begin();

    g_disp = createDisplay();
    if (g_disp && !g_disp->begin()) {
        Serial.println("[display] init FAILED");
        g_disp = nullptr;
    }
    g_ui.begin(g_disp, &g_input);

    net::begin();
    g_buzzer.beep(1);
    Serial.println("type 'help' for commands");
}

void loop() {
    const uint32_t now = millis();

    const uint8_t ev = app::takeEvents();
    if (ev & ctl::EV_FAULT) {
        const ctl::ControllerStatus s = app::status();
        Serial.printf("[FAULT] %s\n", ctl::faultName(s.fault));
        g_ui.toast(ctl::faultName(s.fault), 3000);
        g_buzzer.alarm(true);
    }
    if (ev & ctl::EV_AT_TEMP) {
        g_buzzer.beep(2);
        g_ui.toast("At temperature");
    }
    if (ev & ctl::EV_TUNE_DONE) {
        g_buzzer.beep(3, 150, 150);
        g_ui.toast("Autotune done", 3000);
        const ctl::ControlSettings cs = app::controlSettings();
        Serial.printf("[tune] Kp=%.3f Ki=%.5f Kd=%.1f\n", cs.kp, cs.ki, cs.kd);
    }

    if (now - g_histAt >= History::kIntervalMs) {
        g_histAt = now;
        const ctl::ControllerStatus s = app::status();
        g_history.push(s.meas.procValid, s.meas.procC, s.mode == ctl::Mode::Run ? s.spC : s.targetC);
        g_buzzer.alarm(s.mode == ctl::Mode::Fault);
    }

    g_buzzer.setEnabled(app::settings().buzzer);
    g_buzzer.tick(now);
    g_ui.tick(now);
    net::loop();
    cli::loop();
    app::loopTick();
    delay(2);
}
