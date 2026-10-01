#include "ui.h"

#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "app.h"
#include "history.h"
#include "net/net.h"
#include "settings_fields.h"

using namespace ctl;

// =================================================================== меню
namespace {

enum Item : uint8_t {
    I_RUN, I_TARGET, I_RAMP, I_OUTMAX, I_KP, I_KI, I_KD, I_TUNE, I_TUNERULE, I_TUNEPWR,
    I_MAXWAX, I_MAXHTR, I_HTRREQ, I_NORISE, I_BUZZER, I_AUTORES, I_SENSORS, I_NETWORK, I_ACK, I_BACK,
    I_COUNT
};

enum class Kind : uint8_t { Action, Num, Bool, Enum, Screen };

struct MenuDef {
    const char* label;
    Kind kind;
    const char* key;  // поле ControlSettings (для Num/Bool/Enum)
    float mn, mx, step;
    uint8_t dec;
    const char* unit;
};

// Надписи на дисплее латиницей: встроенный шрифт Adafruit GFX не содержит кириллицы.
const MenuDef kMenu[I_COUNT] = {
    {"Start/Stop", Kind::Action, nullptr, 0, 0, 0, 0, ""},
    {"Setpoint", Kind::Num, "targetC", 20, 95, 0.5f, 1, "C"},
    {"Ramp", Kind::Num, "rampCpm", 0, 30, 0.5f, 1, "C/m"},
    {"Max power", Kind::Num, "outMaxPct", 5, 100, 5, 0, "%"},
    {"Kp", Kind::Num, "kp", 0, 200, 0.5f, 2, ""},
    {"Ki", Kind::Num, "ki", 0, 5, 0.001f, 4, ""},
    {"Kd", Kind::Num, "kd", 0, 5000, 5, 0, ""},
    {"Autotune", Kind::Action, nullptr, 0, 0, 0, 0, ""},
    {"Tune rule", Kind::Enum, "tuneRule", 0, 2, 1, 0, ""},
    {"Tune power", Kind::Num, "tuneOutHighPct", 10, 100, 5, 0, "%"},
    {"Max wax T", Kind::Num, "maxProcessC", 40, 125, 1, 0, "C"},
    {"Max heater T", Kind::Num, "maxHeaterC", 40, 125, 1, 0, "C"},
    {"Heater sens", Kind::Bool, "heaterSensorRequired", 0, 1, 1, 0, ""},
    {"No-rise win", Kind::Num, "runawayWindowMin", 0, 240, 5, 0, "m"},
    {"Buzzer", Kind::Bool, nullptr, 0, 1, 1, 0, ""},
    {"Auto resume", Kind::Bool, "autoResume", 0, 1, 1, 0, ""},
    {"Sensors", Kind::Screen, nullptr, 0, 0, 0, 0, ""},
    {"Network", Kind::Screen, nullptr, 0, 0, 0, 0, ""},
    {"Reset fault", Kind::Action, nullptr, 0, 0, 0, 0, ""},
    {"< Back", Kind::Action, nullptr, 0, 0, 0, 0, ""},
};

const char* kRuleShort[] = {"ZN", "T-L", "NoOS"};

// ----------------------------------------------------------------- рисование
inline int16_t tw(const char* s, uint8_t size) { return (int16_t)strlen(s) * 6 * size; }

void txt(Adafruit_GFX& g, int16_t x, int16_t y, const char* s, uint8_t size, uint16_t col) {
    g.setFont(nullptr);
    g.setTextWrap(false);
    g.setTextSize(size);
    g.setTextColor(col);
    g.setCursor(x, y);
    g.print(s);
}
void txtR(Adafruit_GFX& g, int16_t xr, int16_t y, const char* s, uint8_t size, uint16_t col) {
    txt(g, xr - tw(s, size), y, s, size, col);
}
void txtC(Adafruit_GFX& g, int16_t xc, int16_t y, const char* s, uint8_t size, uint16_t col) {
    txt(g, xc - tw(s, size) / 2, y, s, size, col);
}

void fmtT(char* b, size_t n, bool valid, float v) {
    if (valid) snprintf(b, n, "%.1f", v);
    else snprintf(b, n, "--.-");
}

uint16_t modeColor(Mode m) {
    switch (m) {
        case Mode::Run: return 0x0320;   // тёмно-зелёный
        case Mode::Tune: return 0x0218;  // тёмно-синий
        case Mode::Fault: return pal::ERR;
        default: return 0x4208;          // серый
    }
}

}  // namespace

// =================================================================== жизненный цикл
void Ui::begin(Display* d, InputDevice* in) {
    d_ = d;
    in_ = in;
    lastInputMs_ = millis();
}

void Ui::toast(const char* msg, uint32_t ms) {
    strncpy(toast_, msg, sizeof(toast_) - 1);
    toast_[sizeof(toast_) - 1] = 0;
    toastUntil_ = millis() + ms;
    dirty_ = true;
}

void Ui::tick(uint32_t now) {
    if (in_) handleInput(now);
    if (screen_ != Screen::Main && now - lastInputMs_ > 30000) {
        screen_ = Screen::Main;
        dirty_ = true;
    }
    if (!d_) return;
    if (!dirty_ && now - lastDrawMs_ < 250) return;
    dirty_ = false;
    lastDrawMs_ = now;

    st_ = app::status();
    cs_ = app::controlSettings();
    heaterOn_ = app::heaterOn();
    net::statusLine(net_, sizeof(net_));
    d_->render([this](Adafruit_GFX& g) { draw(g); });
}

// =================================================================== ввод
void Ui::handleInput(uint32_t now) {
    int32_t d = in_->takeDelta();
    const ButtonEvent b = in_->poll(now);
    if (d == 0 && b == ButtonEvent::None) return;
    lastInputMs_ = now;
    dirty_ = true;
    if (d >= 3 || d <= -3) d *= 5;  // ускорение при быстром вращении

    switch (screen_) {
        case Screen::Main:
            if (d) onMainRotate(d, now);
            if (b == ButtonEvent::Click) {
                screen_ = Screen::Menu;
                sel_ = top_ = 0;
            } else if (b == ButtonEvent::Long) {
                const Mode m = app::status().mode;
                if (m == Mode::Fault) toast(app::ack() ? "Fault cleared" : "Fault active!");
                else if (m == Mode::Idle) toast(app::start() ? "Heating ON" : "Cannot start");
                else { app::stop(); toast("STOP"); }
            }
            break;

        case Screen::Menu: {
            int32_t s = (int32_t)sel_ + (d > 0 ? 1 : (d < 0 ? -1 : 0));
            if (s < 0) s = 0;
            if (s >= I_COUNT) s = I_COUNT - 1;
            sel_ = (uint8_t)s;
            if (b == ButtonEvent::Click) onMenuClick(now);
            else if (b == ButtonEvent::Long) screen_ = Screen::Main;
            break;
        }

        case Screen::Edit: {
            const MenuDef& m = kMenu[sel_];
            editVal_ += d * m.step;
            if (editVal_ < m.mn) editVal_ = m.mn;
            if (editVal_ > m.mx) editVal_ = m.mx;
            if (b == ButtonEvent::Click) {
                ControlSettings cs = app::controlSettings();
                if (const FieldDef* f = findField(m.key)) fieldSet(cs, *f, editVal_);
                app::applyControlSettings(cs);
                toast("Saved");
                screen_ = Screen::Menu;
            } else if (b == ButtonEvent::Long) {
                screen_ = Screen::Menu;
            }
            break;
        }

        case Screen::Sensors:
            if (b == ButtonEvent::Click) {
                app::swapSensors();
                toast("Swapped");
            } else if (b == ButtonEvent::Long) {
                screen_ = Screen::Menu;
            }
            break;

        case Screen::Network:
            if (b != ButtonEvent::None) screen_ = Screen::Menu;
            break;
    }
}

void Ui::onMainRotate(int32_t d, uint32_t now) {
    const ControlSettings cs = app::controlSettings();
    app::setTarget(cs.targetC + d * 0.5f);
    setFlashUntil_ = now + 2000;
}

void Ui::onMenuClick(uint32_t now) {
    (void)now;
    const MenuDef& m = kMenu[sel_];
    switch (m.kind) {
        case Kind::Num: {
            const ControlSettings cs = app::controlSettings();
            const FieldDef* f = findField(m.key);
            editVal_ = f ? fieldGet(cs, *f) : 0;
            screen_ = Screen::Edit;
            break;
        }
        case Kind::Bool:
        case Kind::Enum: {
            if (!m.key) {  // зуммер — не в ControlSettings
                app::settings().buzzer = !app::settings().buzzer;
                app::requestSave();
                break;
            }
            ControlSettings cs = app::controlSettings();
            if (const FieldDef* f = findField(m.key)) {
                float v = fieldGet(cs, *f) + 1;
                if (v > m.mx) v = m.mn;
                fieldSet(cs, *f, v);
                app::applyControlSettings(cs);
            }
            break;
        }
        case Kind::Screen:
            screen_ = (sel_ == I_SENSORS) ? Screen::Sensors : Screen::Network;
            break;
        case Kind::Action:
            switch (sel_) {
                case I_RUN: {
                    const Mode md = app::status().mode;
                    if (md == Mode::Idle) toast(app::start() ? "Heating ON" : "Cannot start");
                    else if (md == Mode::Fault) toast("Reset fault first");
                    else { app::stop(); toast("STOP"); }
                    screen_ = Screen::Main;
                    break;
                }
                case I_TUNE:
                    toast(app::startTune() ? "Autotune started" : "Cannot tune");
                    screen_ = Screen::Main;
                    break;
                case I_ACK:
                    toast(app::ack() ? "Fault cleared" : "Fault active!");
                    break;
                case I_BACK:
                    screen_ = Screen::Main;
                    break;
            }
            break;
    }
}

void Ui::menuValue(uint8_t item, char* out, size_t n) {
    out[0] = 0;
    const MenuDef& m = kMenu[item];
    if (item == I_RUN) {
        snprintf(out, n, "%s", st_.mode == Mode::Idle ? "start" : (st_.mode == Mode::Fault ? "-" : "stop"));
        return;
    }
    if (item == I_BUZZER) {
        snprintf(out, n, "%s", app::settings().buzzer ? "on" : "off");
        return;
    }
    if (item == I_ACK) {
        snprintf(out, n, "%s", st_.mode == Mode::Fault ? faultName(st_.fault) : "-");
        return;
    }
    const FieldDef* f = m.key ? findField(m.key) : nullptr;
    if (!f) {
        if (m.kind == Kind::Screen) snprintf(out, n, ">");
        return;
    }
    const float v = fieldGet(cs_, *f);
    if (m.kind == Kind::Bool) snprintf(out, n, "%s", v > 0.5f ? "yes" : "no");
    else if (m.kind == Kind::Enum) snprintf(out, n, "%s", kRuleShort[(int)v % 3]);
    else snprintf(out, n, "%.*f%s", m.dec, v, m.unit);
}

// =================================================================== отрисовка
void Ui::draw(Adafruit_GFX& g) {
    switch (screen_) {
        case Screen::Main: drawMain(g); break;
        case Screen::Menu: drawMenu(g); break;
        case Screen::Edit: drawEdit(g); break;
        case Screen::Sensors: drawInfo(g, false); break;
        case Screen::Network: drawInfo(g, true); break;
    }
    if ((int32_t)(toastUntil_ - millis()) > 0) drawToast(g);
}

void Ui::drawMain(Adafruit_GFX& g) {
    if (d_->height() < 120) drawMainCompact(g);
    else drawMainLarge(g);
}

// --- 128x64 (OLED), 160x80 (TFT 0.96")
void Ui::drawMainCompact(Adafruit_GFX& g) {
    const int16_t W = d_->width(), H = d_->height();
    const bool color = d_->isColor();
    char b[24];

    // Верхняя строка: режим / авария, справа сеть и индикатор SSR
    const char* mode = st_.mode == Mode::Fault ? faultName(st_.fault) : modeName(st_.mode);
    if (color) {
        g.fillRect(0, 0, W, 10, modeColor(st_.mode));
        txt(g, 2, 1, mode, 1, pal::FG);
    } else if (st_.mode == Mode::Fault && (millis() / 500) % 2) {
        g.fillRect(0, 0, W / 2, 9, 1);
        txt(g, 1, 1, mode, 1, 0);
    } else {
        txt(g, 0, 1, mode, 1, 1);
    }
    const net::State ns = net::state();
    const char* nt = ns == net::State::Connected ? (net::mqttConnected() ? "WiFi+M" : "WiFi")
                   : ns == net::State::AccessPoint ? "AP" : ns == net::State::Connecting ? "..." : "";
    txtR(g, W - 10, 1, nt, 1, d_->c(pal::DIM));
    if (heaterOn_) g.fillCircle(W - 4, 4, 3, d_->c(pal::WARN));
    else g.drawCircle(W - 4, 4, 3, d_->c(pal::DIM));

    // Крупно — температура парафина
    fmtT(b, sizeof(b), st_.meas.procValid, st_.meas.procC);
    txt(g, 0, 14, b, 3, d_->c(st_.meas.procValid ? pal::FG : pal::ERR));
    const int16_t xc = tw(b, 3) + 2;
    g.drawCircle(xc + 2, 15, 2, d_->c(pal::FG));
    txt(g, xc + 6, 14, "C", 1, d_->c(pal::FG));

    // Справа — заданная уставка
    const bool flash = (int32_t)(setFlashUntil_ - millis()) > 0;
    const int16_t xr = W;
    txtR(g, xr, 14, "SET", 1, d_->c(flash ? pal::WARN : pal::DIM));
    snprintf(b, sizeof(b), "%.1f", st_.targetC);
    txtR(g, xr, 26, b, 1, d_->c(flash ? pal::WARN : pal::ACCENT));

    // Строка информации
    const int16_t yi = H - 22;
    if (st_.mode == Mode::Tune) {
        snprintf(b, sizeof(b), "TUNE cyc %u", st_.tuneCycles);
    } else if (st_.meas.heaterPresent) {
        char t[10];
        fmtT(t, sizeof(t), st_.meas.heaterValid, st_.meas.heaterC);
        snprintf(b, sizeof(b), "Htr %s", t);
    } else {
        b[0] = 0;
    }
    txt(g, 0, yi, b, 1, d_->c(pal::DIM));
    if (st_.mode == Mode::Run && st_.spC < st_.targetC - 0.05f) {
        snprintf(b, sizeof(b), "R%.1f", st_.spC);
        txtR(g, W, yi, b, 1, d_->c(pal::DIM));
    } else if (st_.mode == Mode::Run && st_.atTemp) {
        txtR(g, W, yi, "OK", 1, d_->c(pal::OK));
    }

    // Полоса мощности
    const int16_t yb = H - 10, bw = W - 30;
    g.drawRect(0, yb, bw, 9, d_->c(pal::FG));
    const int16_t fw = (int16_t)((bw - 4) * st_.outPct / 100.0f);
    if (fw > 0) g.fillRect(2, yb + 2, fw, 5, d_->c(pal::WARN));
    snprintf(b, sizeof(b), "%3.0f%%", st_.outPct);
    txtR(g, W, yb + 1, b, 1, d_->c(pal::FG));
}

// --- 160x128, 240x135, 240x240, 320x240
void Ui::drawMainLarge(Adafruit_GFX& g) {
    const int16_t W = d_->width(), H = d_->height();
    const bool wide = W >= 240;
    const uint8_t hs = wide ? 2 : 1;  // масштаб мелкого текста
    char b[32];

    // Шапка
    const int16_t hdr = wide ? 24 : 14;
    g.fillRect(0, 0, W, hdr, modeColor(st_.mode));
    const char* mode = modeName(st_.mode);
    txt(g, 4, (hdr - 8 * hs) / 2, mode, hs, pal::FG);
    if (st_.mode == Mode::Fault) txt(g, 4 + tw(mode, hs) + 6 * hs, (hdr - 8 * hs) / 2, faultName(st_.fault), hs, pal::FG);
    if (st_.mode != Mode::Fault) txtR(g, W - 14, (hdr - 8) / 2, net_, 1, pal::FG);
    if (heaterOn_) g.fillCircle(W - 6, hdr / 2, 4, pal::WARN);
    else g.drawCircle(W - 6, hdr / 2, 4, pal::DIM);

    // Температура парафина
    fmtT(b, sizeof(b), st_.meas.procValid, st_.meas.procC);
    g.setFont(wide ? &FreeSansBold24pt7b : &FreeSansBold18pt7b);
    g.setTextSize(1);
    g.setTextColor(st_.meas.procValid ? pal::FG : pal::ERR);
    const int16_t base = hdr + (wide ? 44 : 32);
    g.setCursor(6, base);
    g.print(b);
    const int16_t xAfter = g.getCursorX();
    g.setFont(nullptr);
    g.drawCircle(xAfter + 4, base - (wide ? 30 : 22), 3, pal::FG);
    txt(g, xAfter + 9, base - (wide ? 32 : 24), "C", hs, pal::FG);

    // Уставка и ТЭН справа
    const bool flash = (int32_t)(setFlashUntil_ - millis()) > 0;
    const int16_t xr = W - 6;
    if (wide) {
        txtR(g, xr, hdr + 8, "SET", 1, flash ? pal::WARN : pal::DIM);
        snprintf(b, sizeof(b), "%.1f", st_.targetC);
        txtR(g, xr, hdr + 18, b, 3, flash ? pal::WARN : pal::ACCENT);
        if (st_.meas.heaterPresent) {
            txtR(g, xr, hdr + 46, "HEATER", 1, pal::DIM);
            fmtT(b, sizeof(b), st_.meas.heaterValid, st_.meas.heaterC);
            txtR(g, xr, hdr + 56, b, 2, st_.meas.heaterValid ? pal::FG : pal::ERR);
        }
    } else {
        snprintf(b, sizeof(b), "SET %.1f", st_.targetC);
        txtR(g, xr, hdr + 4, b, 1, flash ? pal::WARN : pal::ACCENT);
        if (st_.meas.heaterPresent) {
            char t[10];
            fmtT(t, sizeof(t), st_.meas.heaterValid, st_.meas.heaterC);
            snprintf(b, sizeof(b), "HTR %s", t);
            txtR(g, xr, hdr + 16, b, 1, pal::DIM);
        }
    }

    // Мощность
    const int16_t yb = base + (wide ? 14 : 8);
    const int16_t bh = wide ? 16 : 10;
    const int16_t lw = wide ? 60 : 34;
    snprintf(b, sizeof(b), "%3.0f%%", st_.outPct);
    txt(g, 6, yb + (bh - 8 * hs) / 2 + (wide ? 0 : 1), b, hs, pal::FG);
    const int16_t bx = 6 + lw;
    const int16_t bwid = W - bx - 6 - ((wide && st_.meas.heaterPresent) ? 84 : 0);  // справа — T ТЭНа
    g.drawRect(bx, yb, bwid, bh, pal::DIM);
    const int16_t fw = (int16_t)((bwid - 4) * st_.outPct / 100.0f);
    if (fw > 0) g.fillRect(bx + 2, yb + 2, fw, bh - 4, heaterOn_ ? pal::WARN : 0x8200);

    // Строка состояния
    const int16_t ys = yb + bh + 4;
    b[0] = 0;
    uint16_t sc = pal::DIM;
    if (st_.mode == Mode::Fault) {
        snprintf(b, sizeof(b), "Hold button to reset");
        sc = pal::ERR;
    } else if (st_.mode == Mode::Tune) {
        snprintf(b, sizeof(b), "Autotune: cycle %u", st_.tuneCycles);
        sc = pal::ACCENT;
    } else if (st_.mode == Mode::Run && st_.spC < st_.targetC - 0.05f) {
        snprintf(b, sizeof(b), "Ramp SP %.1f C", st_.spC);
    } else if (st_.mode == Mode::Run && st_.atTemp) {
        snprintf(b, sizeof(b), "At temperature");
        sc = pal::OK;
    } else if (st_.mode == Mode::Idle) {
        snprintf(b, sizeof(b), "Hold button to start");
    }
    txt(g, 6, ys, b, 1, sc);

    // График
    const int16_t gy = ys + 12;
    if (H - gy >= 30) drawGraph(g, 0, gy, W, H - gy);
}

void Ui::drawGraph(Adafruit_GFX& g, int16_t x, int16_t y, int16_t w, int16_t h) {
    g.drawRect(x, y, w, h, pal::GRID);
    const uint16_t n = g_history.count();
    if (n < 2) {
        txtC(g, x + w / 2, y + h / 2 - 4, "collecting history...", 1, pal::DIM);
        return;
    }
    const int16_t pw = w - 2;
    const uint16_t pts = n < pw ? n : pw;
    const uint16_t first = n - pts;
    int16_t lo = INT16_MAX, hi = INT16_MIN;
    for (uint16_t i = first; i < n; ++i) {
        const int16_t v = g_history.pv10(i), s = g_history.sp10(i);
        if (v != History::kInvalid) { if (v < lo) lo = v; if (v > hi) hi = v; }
        if (s < lo) lo = s;
        if (s > hi) hi = s;
    }
    if (hi - lo < 40) {  // минимум 4 °C по вертикали
        const int16_t mid = (hi + lo) / 2;
        lo = mid - 20;
        hi = mid + 20;
    }
    lo -= 5;
    hi += 5;
    auto Y = [&](int16_t v10) { return (int16_t)(y + h - 2 - (int32_t)(v10 - lo) * (h - 4) / (hi - lo)); };
    // шкала
    char b[10];
    snprintf(b, sizeof(b), "%d", hi / 10);
    txt(g, x + 3, y + 3, b, 1, pal::DIM);
    snprintf(b, sizeof(b), "%d", lo / 10);
    txt(g, x + 3, y + h - 11, b, 1, pal::DIM);
    txtR(g, x + w - 3, y + h - 11, "30 min", 1, pal::DIM);

    int16_t px = -1, ppy = 0, psy = 0;
    for (uint16_t i = first; i < n; ++i) {
        const int16_t cx = x + 1 + (int16_t)(i - first) * pw / pts;
        const int16_t v = g_history.pv10(i);
        const int16_t sy = Y(g_history.sp10(i));
        if (px >= 0) {
            if ((cx & 3) < 2) g.drawLine(px, psy, cx, sy, pal::DIM);  // уставка — пунктир
            if (v != History::kInvalid && ppy >= 0) g.drawLine(px, ppy, cx, Y(v), pal::ACCENT);
        }
        px = cx;
        psy = sy;
        ppy = (v != History::kInvalid) ? Y(v) : -1;
    }
}

void Ui::drawMenu(Adafruit_GFX& g) {
    const int16_t W = d_->width(), H = d_->height();
    const uint8_t ts = W >= 240 ? 2 : 1;
    const int16_t rowH = 8 * ts + (ts == 1 ? 2 : 6);
    const int16_t titleH = rowH;
    const bool color = d_->isColor();

    if (color) g.fillRect(0, 0, W, titleH, 0x2104);
    txt(g, 2, (titleH - 8 * ts) / 2, "MENU", ts, d_->c(pal::ACCENT));
    char pos[8];
    snprintf(pos, sizeof(pos), "%u/%u", sel_ + 1, I_COUNT);
    txtR(g, W - 2, (titleH - 8 * ts) / 2, pos, 1, d_->c(pal::DIM));
    if (!color) g.drawFastHLine(0, titleH - 1, W, 1);

    const uint8_t rows = (H - titleH) / rowH;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    char val[20];
    for (uint8_t r = 0; r < rows && top_ + r < I_COUNT; ++r) {
        const uint8_t it = top_ + r;
        const int16_t y = titleH + r * rowH;
        const bool s = it == sel_;
        uint16_t fg = d_->c(pal::FG);
        if (s) {
            g.fillRect(0, y, W, rowH, color ? pal::SEL : 1);
            if (!color) fg = 0;
        }
        const int16_t ty = y + (rowH - 8 * ts) / 2;
        txt(g, 3, ty, kMenu[it].label, ts, fg);
        menuValue(it, val, sizeof(val));
        txtR(g, W - 3, ty, val, ts, s ? fg : d_->c(pal::ACCENT));
    }
}

void Ui::drawEdit(Adafruit_GFX& g) {
    const int16_t W = d_->width(), H = d_->height();
    const MenuDef& m = kMenu[sel_];
    const uint8_t ts = W >= 240 ? 2 : 1;
    txtC(g, W / 2, 2, m.label, ts, d_->c(pal::ACCENT));
    char b[20];
    snprintf(b, sizeof(b), "%.*f", m.dec, editVal_);
    uint8_t big = H >= 200 ? 5 : (H >= 120 ? 4 : 3);
    while (big > 1 && tw(b, big) > W - 8) big--;
    const int16_t yv = H / 2 - 4 * big;
    if (H < 100) {  // мало места по вертикали — единица справа от значения
        const int16_t x0 = (W - tw(b, big) - tw(m.unit, 1) - 3) / 2;
        txt(g, x0, yv, b, big, d_->c(pal::FG));
        txt(g, x0 + tw(b, big) + 3, yv, m.unit, 1, d_->c(pal::DIM));
    } else {
        txtC(g, W / 2, yv, b, big, d_->c(pal::FG));
        txtC(g, W / 2, yv + 8 * big + 4, m.unit, ts, d_->c(pal::DIM));
    }
    txtC(g, W / 2, H - 9, "click=OK  hold=cancel", 1, d_->c(pal::DIM));
}

void Ui::drawInfo(Adafruit_GFX& g, bool network) {
    const int16_t W = d_->width(), H = d_->height();
    const uint8_t ts = W >= 240 ? 2 : 1;
    const int16_t lh = 8 * ts + 3;
    char b[48];
    int16_t y = 2;
    auto line = [&](const char* s, uint16_t c) {
        txt(g, 2, y, s, ts, d_->c(c));
        y += lh;
    };
    if (network) {
        line("NETWORK", pal::ACCENT);
#if HAS_WIFI
        const AppSettings& cfg = app::settings();
        snprintf(b, sizeof(b), "SSID %s", cfg.net.ssid[0] ? cfg.net.ssid : "-");
        line(b, pal::FG);
        char ip[20];
        net::ipString(ip, sizeof(ip));
        const net::State s = net::state();
        snprintf(b, sizeof(b), "%s %s", s == net::State::AccessPoint ? "AP" : "IP", ip);
        line(b, pal::FG);
        if (s == net::State::Connected) {
            snprintf(b, sizeof(b), "RSSI %d dBm", net::rssi());
            line(b, pal::DIM);
        }
        snprintf(b, sizeof(b), "MQTT %s", cfg.net.mqttHost[0] ? (net::mqttConnected() ? "online" : "offline") : "off");
        line(b, pal::FG);
#else
        line("no Wi-Fi on this board", pal::DIM);
#endif
        snprintf(b, sizeof(b), "ID %s", app::deviceId());
        line(b, pal::DIM);
        snprintf(b, sizeof(b), "FW %s", FW_VERSION);
        line(b, pal::DIM);
    } else {
        const app::SensorInfo si = app::sensorInfo();
        snprintf(b, sizeof(b), "SENSORS found %u", si.count);
        line(b, pal::ACCENT);
        char t[10];
        fmtT(t, sizeof(t), st_.meas.procValid, st_.meas.procC);
        snprintf(b, sizeof(b), "Wax  %s C", t);
        line(b, si.procPresent ? pal::FG : pal::ERR);
        line(si.procPresent ? si.procAddr : "missing", pal::DIM);
        fmtT(t, sizeof(t), st_.meas.heaterValid, st_.meas.heaterC);
        snprintf(b, sizeof(b), "Heater %s C", si.heaterPresent ? t : "-");
        line(b, si.heaterPresent ? pal::FG : pal::DIM);
        line(si.heaterPresent ? si.heaterAddr : "not used", pal::DIM);
        txt(g, 2, H - 9, "click=swap hold=back", 1, d_->c(pal::DIM));
    }
}

void Ui::drawToast(Adafruit_GFX& g) {
    const int16_t W = d_->width(), H = d_->height();
    const uint8_t ts = W >= 240 ? 2 : 1;
    int16_t w = tw(toast_, ts) + 12;
    if (w > W - 4) w = W - 4;
    const int16_t h = 8 * ts + 10;
    const int16_t x = (W - w) / 2, y = (H - h) / 2;
    g.fillRect(x, y, w, h, d_->c(pal::BG));
    g.drawRect(x, y, w, h, d_->c(pal::ACCENT));
    txtC(g, W / 2, y + 5, toast_, ts, d_->c(pal::FG));
}
