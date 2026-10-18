// Сканер шины I2C: проверка подключения OLED (SSD1306/SH1106 отвечают на 0x3C или 0x3D).
// Сборка: pio run -e esp32s3-i2cscan -t upload ; лог: pio device monitor
// SSR-пин принудительно держим в LOW — нагрев в этом тесте невозможен.
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

void setup() {
    pinMode(PIN_SSR, OUTPUT);
    digitalWrite(PIN_SSR, LOW);
    Serial.begin(115200);
    delay(1500);
    Serial.printf("\n=== I2C scan: SDA=%d SCL=%d ===\n", PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);
}

void loop() {
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  found 0x%02X%s\n", a, (a == 0x3C || a == 0x3D) ? "  <- OLED" : "");
            found++;
        }
    }
    if (!found) Serial.println("  nothing: check wiring SDA/SCL, VCC 3.3V, GND");
    Serial.println("---");
    delay(3000);
}
