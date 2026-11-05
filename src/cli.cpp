// Консоль через USB-Serial (115200). Команда help — список.
#include "cli.h"

#include "app.h"
#include "hal.h"
#include "net/net.h"
#include "settings_fields.h"
#if SIM_SENSORS
#include <math.h>
#include <stdlib.h>
#endif

namespace {
char g_line[160];
size_t g_len = 0;

void printStatus() {
    const ctl::ControllerStatus s = app::status();
    Serial.printf("mode=%s fault=%s pv=%s%.2f heater=%s%.2f target=%.1f sp=%.2f out=%.1f%% ssr=%d P=%.1f I=%.1f D=%.1f\n",
                  ctl::modeName(s.mode), ctl::faultName(s.fault), s.meas.procValid ? "" : "!", s.meas.procC,
                  s.meas.heaterPresent ? (s.meas.heaterValid ? "" : "!") : "-", s.meas.heaterC, s.targetC, s.spC,
                  s.outPct, app::heaterOn(), s.p, s.i, s.d);
}

void printSettings() {
    const ctl::ControlSettings cs = app::controlSettings();
    for (size_t k = 0; k < kFieldCount; ++k)
        Serial.printf("  %-22s %-10g %s\n", kFields[k].key, fieldGet(cs, kFields[k]), kFields[k].help);
    const NetSettings& n = app::settings().net;
    Serial.printf("  wifi ssid='%s' host='%s' mqtt='%s:%u' user='%s' base='%s' ha=%u web_pass=%s buzzer=%u\n", n.ssid,
                  n.hostname, n.mqttHost, n.mqttPort, n.mqttUser, n.mqttBase, n.mqttHaDiscovery,
                  n.webPass[0] ? "set" : "none", app::settings().buzzer);
}

void help() {
    Serial.println(
        "Commands:\n"
        "  status | show | sensors\n"
        "  start | stop | tune | ack\n"
        "  sp <C>                  уставка\n"
        "  pid <kp> <ki> <kd>\n"
        "  set <key> <value>       любое поле из 'show'\n"
        "  wifi <ssid> [pass]      (пустой ssid: wifi -) затем reboot\n"
        "  mqtt <host> [port] [user] [pass]   (mqtt - выключить)\n"
        "  webpass <pass|->        пароль веб-интерфейса (логин admin)\n"
        "  hostname <name> | buzzer <0|1> | swap | rescan\n"
        "  save | reboot | factory"
#if SIM_SENSORS
        "\n  sim                     состояние модели\n"
        "  sim speed <x>           ускорение времени (0.1..100)\n"
        "  sim wax <C> | heater <C> | amb <C>   (-20..150)\n"
        "  sim power <W>           мощность ТЭНа (0..3000)\n"
        "  sim fail <0|1>          обрыв датчика парафина\n"
        "  sim bus <0|1>           зависание шины 1-Wire (нет измерений)\n"
        "  sim stuck <0|1>         пробитый SSR: ТЭН греет при ssr=0\n"
        "  sim hang                зависание цикла управления (WDT)\n"
        "  sim reset               остудить до комнатной"
#endif
    );
}

void copyArg(char* dst, size_t n, const char* src) {
    if (!src || !strcmp(src, "-")) src = "";
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

#if SIM_SENSORS
// Строгий разбор числа: "abc", "12x", пустая строка — ошибка (atof дал бы 0).
bool parseNum(const char* s, float& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    out = strtof(s, &end);
    return end != s && *end == 0 && !isnan(out);
}

void printSim() {
    const app::SimState s = app::simState();
    Serial.printf(
        "sim: wax=%.2f heater=%.2f amb=%.1f power=%.0fW melted=%.0f%% speed=x%.1f fail=%d bus=%s stuck=%d ssr=%d\n",
        s.waxC, s.heaterC, s.ambientC, s.powerW, s.meltedPct, s.speed, s.failProc, s.hangBus ? "hang" : "ok",
        s.stuck, app::heaterOn());
}

void simCommand(int argc, char** argv) {
    using app::SimCmd;
    namespace sim = app::sim;
    const char* k = argc >= 2 ? argv[1] : "";
    if (!*k) {
        printSim();
        return;
    }
    SimCmd c;
    if (!strcmp(k, "reset")) {
        c.kind = SimCmd::Reset;
    } else if (!strcmp(k, "hang")) {
        Serial.println("sim: control task hang -> expect WDT reset in ~5 s");
        Serial.flush();
        c.kind = SimCmd::Hang;
        if (!app::simCmd(c)) Serial.println("sim: busy, retry");
        return;
    } else {
        struct Key {
            const char* name;
            SimCmd::Kind kind;
            float lo, hi;
        };
        static const Key kKeys[] = {
            {"speed", SimCmd::Speed, sim::kSpeedMin, sim::kSpeedMax},
            {"wax", SimCmd::Wax, sim::kTempMinC, sim::kTempMaxC},
            {"heater", SimCmd::Heater, sim::kTempMinC, sim::kTempMaxC},
            {"amb", SimCmd::Amb, sim::kTempMinC, sim::kTempMaxC},
            {"power", SimCmd::Power, 0.0f, sim::kPowerMaxW},
            {"fail", SimCmd::Fail, 0.0f, 1.0f},
            {"bus", SimCmd::Bus, 0.0f, 1.0f},
            {"stuck", SimCmd::Stuck, 0.0f, 1.0f},
        };
        const Key* key = nullptr;
        for (const Key& x : kKeys)
            if (!strcmp(k, x.name)) key = &x;
        if (!key) {
            Serial.println("sim: unknown key (help)");
            return;
        }
        float v;
        if (argc < 3 || !parseNum(argv[2], v)) {
            Serial.println("sim: bad value");
            return;
        }
        if (v < key->lo || v > key->hi) {
            Serial.printf("sim: %s out of range %g..%g\n", key->name, key->lo, key->hi);
            return;
        }
        c.kind = key->kind;
        c.v = v;
    }
    if (!app::simCmd(c)) {
        Serial.println("sim: busy, retry");
        return;
    }
    delay(20);  // команду применяет control task (период 5 мс) — ждём, чтобы показать новое состояние
    printSim();
}
#endif

void exec(char* line) {
    char* argv[6] = {};
    int argc = 0;
    for (char* tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
    if (!argc) return;
    const char* c = argv[0];
    AppSettings& cfg = app::settings();

    if (!strcmp(c, "help") || !strcmp(c, "?")) help();
    else if (!strcmp(c, "status")) printStatus();
    else if (!strcmp(c, "show")) printSettings();
    else if (!strcmp(c, "start")) Serial.println(app::start() ? "ok" : "refused (fault?)");
    else if (!strcmp(c, "stop")) { app::stop(); Serial.println("ok"); }
    else if (!strcmp(c, "tune")) Serial.println(app::startTune() ? "autotune started" : "refused");
    else if (!strcmp(c, "ack")) Serial.println(app::ack() ? "cleared" : "fault condition still present");
    else if (!strcmp(c, "sp") && argc >= 2) { app::setTarget(atof(argv[1])); Serial.println("ok"); }
    else if (!strcmp(c, "pid") && argc >= 4) {
        ctl::ControlSettings cs = app::controlSettings();
        cs.kp = atof(argv[1]);
        cs.ki = atof(argv[2]);
        cs.kd = atof(argv[3]);
        app::applyControlSettings(cs);
        Serial.println("ok");
    } else if (!strcmp(c, "set") && argc >= 3) {
        const FieldDef* f = findField(argv[1]);
        if (!f) { Serial.println("unknown key, see 'show'"); return; }
        ctl::ControlSettings cs = app::controlSettings();
        fieldSet(cs, *f, atof(argv[2]));
        app::applyControlSettings(cs);
        Serial.printf("%s = %g\n", f->key, fieldGet(app::controlSettings(), *f));
    } else if (!strcmp(c, "wifi") && argc >= 2) {
        copyArg(cfg.net.ssid, sizeof(cfg.net.ssid), argv[1]);
        copyArg(cfg.net.pass, sizeof(cfg.net.pass), argc >= 3 ? argv[2] : "");
        app::saveNow();
        Serial.println("saved, 'reboot' to apply");
    } else if (!strcmp(c, "mqtt") && argc >= 2) {
        copyArg(cfg.net.mqttHost, sizeof(cfg.net.mqttHost), argv[1]);
        if (argc >= 3) cfg.net.mqttPort = atoi(argv[2]);
        copyArg(cfg.net.mqttUser, sizeof(cfg.net.mqttUser), argc >= 4 ? argv[3] : "");
        copyArg(cfg.net.mqttPass, sizeof(cfg.net.mqttPass), argc >= 5 ? argv[4] : "");
        app::saveNow();
        Serial.println("saved, 'reboot' to apply");
    } else if (!strcmp(c, "webpass") && argc >= 2) {
        copyArg(cfg.net.webPass, sizeof(cfg.net.webPass), argv[1]);
        app::saveNow();
        Serial.println("ok");
    } else if (!strcmp(c, "hostname") && argc >= 2) {
        copyArg(cfg.net.hostname, sizeof(cfg.net.hostname), argv[1]);
        app::saveNow();
        Serial.println("saved, 'reboot' to apply");
    } else if (!strcmp(c, "buzzer") && argc >= 2) {
        cfg.buzzer = atoi(argv[1]) ? 1 : 0;
        app::saveNow();
        Serial.println("ok");
    } else if (!strcmp(c, "sensors")) {
        const app::SensorInfo si = app::sensorInfo();
        Serial.printf("found=%u wax=%s%s heater=%s%s\n", si.count, si.procAddr, si.procPresent ? "" : "(missing)",
                      si.heaterAddr, si.heaterPresent ? "" : "(none)");
    } else if (!strcmp(c, "swap")) { app::swapSensors(); Serial.println("ok"); }
    else if (!strcmp(c, "rescan")) { app::rescanSensors(); Serial.println("ok"); }
    else if (!strcmp(c, "save")) app::saveNow();
    else if (!strcmp(c, "reboot")) { app::stop(); delay(100); hal::reboot(); }
    else if (!strcmp(c, "factory")) app::factoryReset();
#if SIM_SENSORS
    else if (!strcmp(c, "sim")) simCommand(argc, argv);
#endif
    else Serial.println("? (help)");
}
}  // namespace

namespace cli {
void loop() {
    while (Serial.available()) {
        const int ch = Serial.read();
        if (ch == '\r' || ch == '\n') {
            if (g_len) {
                g_line[g_len] = 0;
                Serial.printf("> %s\n", g_line);
                exec(g_line);
                g_len = 0;
            }
        } else if (g_len < sizeof(g_line) - 1) {
            g_line[g_len++] = (char)ch;
        }
    }
}
}  // namespace cli
