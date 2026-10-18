// Аппаратный тест дисплея Waveshare RP2350-LCD-0.96 (ST7735S 160x80, SPI1).
// Сборка: pio run -e pico2-lcd096-test -t upload ; лог: pio device monitor
// Кнопка BOOT — переключение варианта инициализации (TAB/смещения).
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "boards/board_rp2350_lcd096.h"

class TestTft : public Adafruit_ST7735 {
public:
    using Adafruit_ST7735::Adafruit_ST7735;
    void offsets(int8_t col, int8_t row) { setColRowStart(col, row); }
};

struct Mode { const char* name; uint8_t tab; int8_t col; int8_t row; };
static const Mode MODES[] = {
    {"PLUGIN, Adafruit offs", INITR_MINI160x80_PLUGIN, -1, -1},
    {"PLUGIN, 26/1 (WS)",     INITR_MINI160x80_PLUGIN, 26, 1},
    {"MINI,   26/1 (WS)",     INITR_MINI160x80,        26, 1},
};
static const int NMODES = sizeof(MODES) / sizeof(MODES[0]);

TestTft tft(&SPI1, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
int mode = 0;

void initDisplay(const Mode& m) {
    tft.initR(m.tab);
    if (m.col >= 0) tft.offsets(m.col, m.row);
    tft.setRotation(1);  // альбом 160x80
    tft.setSPISpeed(27000000);
    Serial.printf("[mode %d] %s  %dx%d\n", mode, m.name, tft.width(), tft.height());
}

void frameTest() {
    // Рамка 1 px + цветные углы: пропала сторона или мусор по краю = неверные смещения
    tft.fillScreen(ST77XX_BLACK);
    tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_WHITE);
    tft.fillRect(0, 0, 6, 6, ST77XX_RED);
    tft.fillRect(tft.width() - 6, 0, 6, 6, ST77XX_GREEN);
    tft.fillRect(0, tft.height() - 6, 6, 6, ST77XX_BLUE);
    tft.fillRect(tft.width() - 6, tft.height() - 6, 6, 6, ST77XX_YELLOW);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 10); tft.printf("mode %d/%d", mode, NMODES - 1);
    tft.setCursor(10, 22); tft.print(MODES[mode].name);
    tft.setCursor(10, 36); tft.print("R G");
    tft.setCursor(10, 46); tft.print("B Y  <- corners");
    tft.setCursor(10, 62); tft.print("BOOT = next mode");
}

void colorTest() {
    // Красный должен быть красным: синий => RGB/BGR, голубой => нужна другая инверсия (TAB)
    const uint16_t c[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE, ST77XX_BLACK};
    const char* n[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
    for (int i = 0; i < 5; i++) {
        tft.fillScreen(c[i]);
        tft.setTextColor(i == 3 ? ST77XX_BLACK : ST77XX_WHITE);
        tft.setTextSize(2);
        tft.setCursor(40, 32); tft.print(n[i]);
        delay(700);
    }
}

void tempMock() {
    // Макет рабочего экрана и замер скорости заливки
    uint32_t t0 = micros();
    tft.fillScreen(ST77XX_BLACK);
    uint32_t dt = micros() - t0;
    tft.setTextColor(ST77XX_ORANGE); tft.setTextSize(3);
    tft.setCursor(4, 8); tft.print("62.4");
    tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4, 44); tft.print("SET 65.0 C  OUT 48%");
    tft.fillRect(4, 58, 152 * 48 / 100, 10, ST77XX_RED);
    tft.drawRect(4, 58, 152, 10, ST77XX_WHITE);
    Serial.printf("fillScreen: %lu us\n", (unsigned long)dt);
}

bool bootPressed() {
    static bool last = false;
    bool now = BOOTSEL;
    bool edge = now && !last;
    last = now;
    return edge;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== RP2350-LCD-0.96 display test ===");
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
    SPI1.setSCK(PIN_TFT_SCK);
    SPI1.setTX(PIN_TFT_MOSI);
    initDisplay(MODES[mode]);
    colorTest();
    tempMock(); delay(1500);
    frameTest();
}

void loop() {
    if (bootPressed()) {
        mode = (mode + 1) % NMODES;
        initDisplay(MODES[mode]);
        frameTest();
    }
    // Плавная регулировка подсветки через Serial: '0'..'9'
    if (Serial.available()) {
        int ch = Serial.read();
        if (ch >= '0' && ch <= '9') {
            analogWrite(PIN_TFT_BL, (ch - '0') * 255 / 9);
            Serial.printf("backlight %d/9\n", ch - '0');
        }
    }
    delay(20);
}
