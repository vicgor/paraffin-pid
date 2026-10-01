#include "net.h"

#include "../hal.h"

#if !HAS_WIFI
// ---------------------------------------------------------------- заглушки (Pico 2 без Wi-Fi)
namespace net {
void begin() {}
void loop() {}
State state() { return State::Off; }
bool mqttConnected() { return false; }
void statusLine(char* out, size_t n) { snprintf(out, n, "no WiFi"); }
void ipString(char* out, size_t n) { snprintf(out, n, "-"); }
int rssi() { return 0; }
}  // namespace net
#else

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#if PLATFORM_ESP32
#include <ESPmDNS.h>
#else
#include <LEAmDNS.h>
#endif

#include "../app.h"
#include "../history.h"
#include "../settings_fields.h"
#include "web_page.h"

namespace net {
namespace {

WebServer g_server(80);
WiFiClient g_wifiClient;
PubSubClient g_mqtt(g_wifiClient);
State g_state = State::Off;
uint32_t g_t0 = 0;
bool g_serverStarted = false;
bool g_rebootReq = false;
uint32_t g_rebootAt = 0;
char g_base[64] = "";
uint32_t g_mqttRetryAt = 0, g_mqttPubAt = 0;
ctl::Mode g_lastMode = ctl::Mode::Idle;

NetSettings& cfg() { return app::settings().net; }

// ============================================================== Web
bool auth() {
    if (!cfg().webPass[0]) return true;
    if (g_server.authenticate("admin", cfg().webPass)) return true;
    g_server.requestAuthentication();
    return false;
}

void sendJson(JsonDocument& doc, int code = 200) {
    String s;
    serializeJson(doc, s);
    g_server.send(code, "application/json", s);
}

void statusJson(JsonDocument& d) {
    const ctl::ControllerStatus st = app::status();
    d["mode"] = ctl::modeName(st.mode);
    d["fault"] = (int)st.fault;
    d["faultName"] = ctl::faultName(st.fault);
    d["faultText"] = ctl::faultText(st.fault);
    d["pv"] = st.meas.procC;
    d["pvValid"] = st.meas.procValid;
    d["heater"] = st.meas.heaterC;
    d["heaterPresent"] = st.meas.heaterPresent;
    d["heaterValid"] = st.meas.heaterValid;
    d["target"] = st.targetC;
    d["sp"] = st.spC;
    d["out"] = st.outPct;
    d["ssr"] = app::heaterOn();
    d["p"] = st.p;
    d["i"] = st.i;
    d["d"] = st.d;
    d["atTemp"] = st.atTemp;
    d["tuneCycles"] = st.tuneCycles;
    d["ku"] = st.ku;
    d["tu"] = st.tu;
    d["uptime"] = millis() / 1000;
    d["rssi"] = g_state == State::Connected ? WiFi.RSSI() : 0;
    d["fw"] = FW_VERSION;
    d["chip"] = hal::chipName();
    d["id"] = app::deviceId();
}

void hStatus() {
    if (!auth()) return;
    JsonDocument d;
    statusJson(d);
    sendJson(d);
}

void hHistory() {
    if (!auth()) return;
    String s;
    const uint16_t n = g_history.count();
    s.reserve(n * 12 + 64);
    s = "{\"interval\":";
    s += History::kIntervalMs / 1000;
    s += ",\"pv\":[";
    char b[12];
    for (uint16_t i = 0; i < n; ++i) {
        const int16_t v = g_history.pv10(i);
        if (v == History::kInvalid) snprintf(b, sizeof(b), "%snull", i ? "," : "");
        else snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", v / 10.0f);
        s += b;
    }
    s += "],\"sp\":[";
    for (uint16_t i = 0; i < n; ++i) {
        snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", g_history.sp10(i) / 10.0f);
        s += b;
    }
    s += "]}";
    g_server.send(200, "application/json", s);
}

bool runCommand(const char* c, float value, const char** err) {
    *err = "";
    if (!strcmp(c, "start")) {
        if (!app::start()) { *err = "Сначала сбросьте аварию"; return false; }
    } else if (!strcmp(c, "stop")) {
        app::stop();
    } else if (!strcmp(c, "tune")) {
        if (!app::startTune()) { *err = "Нельзя запустить автотюнинг (авария или нет датчика)"; return false; }
    } else if (!strcmp(c, "ack")) {
        if (!app::ack()) { *err = "Причина аварии не устранена"; return false; }
    } else if (!strcmp(c, "target")) {
        if (isnan(value)) { *err = "Нет значения"; return false; }
        app::setTarget(value);
    } else {
        *err = "Неизвестная команда";
        return false;
    }
    return true;
}

void hCmd() {
    if (!auth()) return;
    JsonDocument in;
    if (deserializeJson(in, g_server.arg("plain"))) {
        g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    const char* err;
    const bool ok = runCommand(in["cmd"] | "", in["value"] | NAN, &err);
    JsonDocument out;
    out["ok"] = ok;
    if (!ok) out["error"] = err;
    sendJson(out, ok ? 200 : 409);
}

void hSettingsGet() {
    if (!auth()) return;
    const ctl::ControlSettings cs = app::controlSettings();
    JsonDocument d;
    JsonArray arr = d["fields"].to<JsonArray>();
    for (size_t k = 0; k < kFieldCount; ++k) {
        JsonObject o = arr.add<JsonObject>();
        o["key"] = kFields[k].key;
        o["value"] = fieldGet(cs, kFields[k]);
        o["help"] = kFields[k].help;
    }
    JsonObject n = d["net"].to<JsonObject>();
    n["ssid"] = cfg().ssid;
    n["hostname"] = cfg().hostname;
    n["mqttHost"] = cfg().mqttHost;
    n["mqttPort"] = cfg().mqttPort;
    n["mqttUser"] = cfg().mqttUser;
    n["mqttBase"] = cfg().mqttBase;
    n["mqttHa"] = cfg().mqttHaDiscovery;
    n["buzzer"] = app::settings().buzzer;
    sendJson(d);
}

void copyStr(char* dst, size_t n, JsonVariantConst v) {
    if (v.isNull()) return;
    strncpy(dst, v.as<const char*>() ? v.as<const char*>() : "", n - 1);
    dst[n - 1] = 0;
}

void hSettingsPost() {
    if (!auth()) return;
    JsonDocument in;
    if (deserializeJson(in, g_server.arg("plain"))) {
        g_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    if (in["ctl"].is<JsonObjectConst>()) {
        ctl::ControlSettings cs = app::controlSettings();
        for (JsonPairConst kv : in["ctl"].as<JsonObjectConst>()) {
            if (const FieldDef* f = findField(kv.key().c_str())) fieldSet(cs, *f, kv.value().as<float>());
        }
        app::applyControlSettings(cs);
    }
    if (in["net"].is<JsonObjectConst>()) {
        JsonObjectConst n = in["net"];
        NetSettings& c = cfg();
        copyStr(c.ssid, sizeof(c.ssid), n["ssid"]);
        copyStr(c.pass, sizeof(c.pass), n["pass"]);
        copyStr(c.hostname, sizeof(c.hostname), n["hostname"]);
        copyStr(c.webPass, sizeof(c.webPass), n["webPass"]);
        copyStr(c.mqttHost, sizeof(c.mqttHost), n["mqttHost"]);
        copyStr(c.mqttUser, sizeof(c.mqttUser), n["mqttUser"]);
        copyStr(c.mqttPass, sizeof(c.mqttPass), n["mqttPass"]);
        copyStr(c.mqttBase, sizeof(c.mqttBase), n["mqttBase"]);
        if (!n["mqttPort"].isNull()) c.mqttPort = n["mqttPort"].as<uint16_t>();
        if (!n["mqttHa"].isNull()) c.mqttHaDiscovery = n["mqttHa"].as<uint8_t>() ? 1 : 0;
        if (!n["buzzer"].isNull()) app::settings().buzzer = n["buzzer"].as<uint8_t>() ? 1 : 0;
    }
    app::saveNow();
    g_server.send(200, "application/json", "{\"ok\":true}");
}

void hReboot() {
    if (!auth()) return;
    g_server.send(200, "application/json", "{\"ok\":true}");
    g_rebootReq = true;
    g_rebootAt = millis() + 500;
}

void startServer() {
    if (g_serverStarted) return;
    g_serverStarted = true;
    g_server.on("/", HTTP_GET, [] {
        if (!auth()) return;
        g_server.send(200, "text/html; charset=utf-8", kIndexHtml);
    });
    g_server.on("/api/status", HTTP_GET, hStatus);
    g_server.on("/api/history", HTTP_GET, hHistory);
    g_server.on("/api/cmd", HTTP_POST, hCmd);
    g_server.on("/api/settings", HTTP_GET, hSettingsGet);
    g_server.on("/api/settings", HTTP_POST, hSettingsPost);
    g_server.on("/api/reboot", HTTP_POST, hReboot);
    g_server.onNotFound([] { g_server.send(404, "text/plain", "not found"); });
    g_server.begin();
    if (MDNS.begin(cfg().hostname)) MDNS.addService("http", "tcp", 80);
}

// ============================================================== MQTT
void topic(char* out, size_t n, const char* suffix) { snprintf(out, n, "%s/%s", g_base, suffix); }

void publishDiscovery() {
    char t[128], b[64];
    const char* id = app::deviceId();
    const ctl::ControlSettings cs = app::controlSettings();
    JsonDocument dev;
    dev["ids"][0] = String("paraffin_") + id;
    dev["name"] = "Paraffin melter";
    dev["mf"] = "DIY";
    dev["mdl"] = hal::chipName();
    dev["sw"] = FW_VERSION;

    {  // climate
        JsonDocument d;
        d["name"] = nullptr;
        d["uniq_id"] = String("paraffin_") + id + "_climate";
        topic(b, sizeof(b), "status"); d["avty_t"] = b;
        topic(b, sizeof(b), "state");
        d["curr_temp_t"] = b; d["curr_temp_tpl"] = "{{ value_json.pv }}";
        d["temp_stat_t"] = b; d["temp_stat_tpl"] = "{{ value_json.target }}";
        d["mode_stat_t"] = b;
        d["mode_stat_tpl"] = "{{ 'heat' if value_json.mode in ['HEAT','TUNE'] else 'off' }}";
        d["act_t"] = b;
        d["act_tpl"] = "{{ 'heating' if value_json.ssr else ('idle' if value_json.mode in ['HEAT','TUNE'] else 'off') }}";
        topic(b, sizeof(b), "target/set"); d["temp_cmd_t"] = b;
        topic(b, sizeof(b), "mode/set"); d["mode_cmd_t"] = b;
        d["modes"][0] = "off";
        d["modes"][1] = "heat";
        d["min_temp"] = cs.spMinC;
        d["max_temp"] = cs.spMaxC;
        d["temp_step"] = 0.5;
        d["precision"] = 0.1;
        d["dev"] = dev;
        snprintf(t, sizeof(t), "homeassistant/climate/paraffin_%s/config", id);
        String s;
        serializeJson(d, s);
        g_mqtt.publish(t, s.c_str(), true);
    }
    struct S { const char* key; const char* name; const char* unit; const char* dc; const char* tpl; };
    const S sensors[] = {
        {"out", "Heater power", "%", nullptr, "{{ value_json.out | round(0) }}"},
        {"heater", "Heater temperature", "°C", "temperature", "{{ value_json.heater if value_json.heaterValid else None }}"},
        {"fault", "Fault", nullptr, nullptr, "{{ value_json.faultName }}"},
    };
    for (const S& x : sensors) {
        JsonDocument d;
        d["name"] = x.name;
        d["uniq_id"] = String("paraffin_") + id + "_" + x.key;
        topic(b, sizeof(b), "status"); d["avty_t"] = b;
        topic(b, sizeof(b), "state"); d["stat_t"] = b;
        d["val_tpl"] = x.tpl;
        if (x.unit) d["unit_of_meas"] = x.unit;
        if (x.dc) d["dev_cla"] = x.dc;
        d["dev"] = dev;
        snprintf(t, sizeof(t), "homeassistant/sensor/paraffin_%s_%s/config", id, x.key);
        String s;
        serializeJson(d, s);
        g_mqtt.publish(t, s.c_str(), true);
    }
}

void publishState() {
    JsonDocument d;
    statusJson(d);
    String s;
    serializeJson(d, s);
    char t[80];
    topic(t, sizeof(t), "state");
    g_mqtt.publish(t, s.c_str(), true);
}

void onMqtt(char* tp, uint8_t* payload, unsigned int len) {
    char msg[32];
    const unsigned n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
    memcpy(msg, payload, n);
    msg[n] = 0;
    const char* suffix = tp + strlen(g_base) + 1;
    const char* err;
    if (!strcmp(suffix, "cmd")) runCommand(msg, NAN, &err);
    else if (!strcmp(suffix, "target/set")) runCommand("target", atof(msg), &err);
    else if (!strcmp(suffix, "mode/set")) runCommand(!strcmp(msg, "heat") ? "start" : "stop", NAN, &err);
    g_mqttPubAt = 0;  // сразу опубликовать новое состояние
}

void mqttLoop(uint32_t now) {
    if (!cfg().mqttHost[0] || g_state != State::Connected) return;
    if (!g_mqtt.connected()) {
        if ((int32_t)(now - g_mqttRetryAt) < 0) return;
        g_mqttRetryAt = now + 15000;
        g_mqtt.setServer(cfg().mqttHost, cfg().mqttPort);
        char will[80], cid[32];
        topic(will, sizeof(will), "status");
        snprintf(cid, sizeof(cid), "paraffin-%s", app::deviceId());
        const bool ok = g_mqtt.connect(cid, cfg().mqttUser[0] ? cfg().mqttUser : nullptr,
                                       cfg().mqttUser[0] ? cfg().mqttPass : nullptr, will, 1, true, "offline");
        Serial.printf("[mqtt] connect %s: %s\n", cfg().mqttHost, ok ? "ok" : "fail");
        if (!ok) return;
        g_mqtt.publish(will, "online", true);
        char t[80];
        topic(t, sizeof(t), "cmd");
        g_mqtt.subscribe(t);
        topic(t, sizeof(t), "target/set");
        g_mqtt.subscribe(t);
        topic(t, sizeof(t), "mode/set");
        g_mqtt.subscribe(t);
        if (cfg().mqttHaDiscovery) publishDiscovery();
        g_mqttPubAt = 0;
    }
    g_mqtt.loop();
    const ctl::Mode m = app::status().mode;
    if (m != g_lastMode || (int32_t)(now - g_mqttPubAt) >= 0) {
        g_lastMode = m;
        g_mqttPubAt = now + 5000;
        publishState();
    }
}

// ============================================================== Wi-Fi
void startAP() {
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "Paraffin-%s", app::deviceId() + 4);
#if PLATFORM_ESP32
    WiFi.mode(cfg().ssid[0] ? WIFI_AP_STA : WIFI_AP);  // ESP32 продолжает попытки STA
#else
    WiFi.disconnect();
#endif
    const bool ok = strlen(cfg().apPass) >= 8 ? WiFi.softAP(ssid, cfg().apPass) : WiFi.softAP(ssid);
    Serial.printf("[wifi] AP %s %s, IP %s\n", ssid, ok ? "up" : "FAILED", WiFi.softAPIP().toString().c_str());
    g_state = State::AccessPoint;
    startServer();
}

}  // namespace

void begin() {
    snprintf(g_base, sizeof(g_base), "%s", cfg().mqttBase[0] ? cfg().mqttBase : "");
    if (!g_base[0]) snprintf(g_base, sizeof(g_base), "paraffin/%s", app::deviceId());
    g_mqtt.setBufferSize(1024);
    g_mqtt.setCallback(onMqtt);
    g_mqtt.setSocketTimeout(3);

    WiFi.setHostname(cfg().hostname);
    if (!cfg().ssid[0]) {
        startAP();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg().ssid, cfg().pass);
    g_state = State::Connecting;
    g_t0 = millis();
    Serial.printf("[wifi] connecting to %s\n", cfg().ssid);
}

void loop() {
    const uint32_t now = millis();
    const bool up = WiFi.status() == WL_CONNECTED;
    switch (g_state) {
        case State::Off: break;
        case State::Connecting:
            if (up) {
                g_state = State::Connected;
                Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
                startServer();
            } else if (now - g_t0 > 30000) {
                startAP();
            }
            break;
        case State::Connected:
            if (!up) {
                g_state = State::Connecting;
                g_t0 = now;
#if !PLATFORM_ESP32
                WiFi.begin(cfg().ssid, cfg().pass);
#endif
            }
            break;
        case State::AccessPoint:
#if PLATFORM_ESP32
            if (up) {  // в режиме AP+STA роутер всё-таки нашёлся
                g_state = State::Connected;
                WiFi.mode(WIFI_STA);
                Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
            }
#endif
            break;
    }
    if (g_serverStarted) {
        g_server.handleClient();
#if !PLATFORM_ESP32
        MDNS.update();
#endif
    }
    mqttLoop(now);
    if (g_rebootReq && (int32_t)(now - g_rebootAt) >= 0) hal::reboot();
}

State state() { return g_state; }
bool mqttConnected() { return g_mqtt.connected(); }
int rssi() { return g_state == State::Connected ? WiFi.RSSI() : 0; }

void ipString(char* out, size_t n) {
    if (g_state == State::Connected) snprintf(out, n, "%s", WiFi.localIP().toString().c_str());
    else if (g_state == State::AccessPoint) snprintf(out, n, "%s", WiFi.softAPIP().toString().c_str());
    else snprintf(out, n, "-");
}

void statusLine(char* out, size_t n) {
    char ip[20];
    ipString(ip, sizeof(ip));
    switch (g_state) {
        case State::Connected: snprintf(out, n, "%s%s", ip, g_mqtt.connected() ? " M" : ""); break;
        case State::AccessPoint: snprintf(out, n, "AP %s", ip); break;
        case State::Connecting: snprintf(out, n, "WiFi..."); break;
        default: snprintf(out, n, "-"); break;
    }
}

}  // namespace net
#endif
