#include "hal.h"

#if PLATFORM_ESP32
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#else
#include <hardware/watchdog.h>
#include <pico/unique_id.h>
#endif

namespace hal {

// ---------------------------------------------------------------- Mutex
#if PLATFORM_ESP32
Mutex::Mutex() : h_(xSemaphoreCreateMutex()) {}
void Mutex::lock() { xSemaphoreTake(h_, portMAX_DELAY); }
void Mutex::unlock() { xSemaphoreGive(h_); }
#else
Mutex::Mutex() { mutex_init(&m_); }
void Mutex::lock() { mutex_enter_blocking(&m_); }
void Mutex::unlock() { mutex_exit(&m_); }
#endif

// ---------------------------------------------------------------- Цикл управления
static TickFn s_tick = nullptr;
static uint32_t s_period = 5;

#if PLATFORM_ESP32
static void controlTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        s_tick();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(s_period) ? pdMS_TO_TICKS(s_period) : 1);
    }
}

void startControlLoop(TickFn fn, uint32_t periodMs) {
    s_tick = fn;
    s_period = periodMs;
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(controlTask, "control", 6144, nullptr, 5, nullptr);
#else
    // loop() и Wi-Fi-стек работают на разных ядрах; управление — на ядре приложения,
    // но с приоритетом выше loop().
    xTaskCreatePinnedToCore(controlTask, "control", 6144, nullptr, 5, nullptr, ARDUINO_RUNNING_CORE);
#endif
}
#else
static volatile bool s_started = false;
void startControlLoop(TickFn fn, uint32_t periodMs) {
    s_tick = fn;
    s_period = periodMs;
    s_started = true;
}
#endif

// ---------------------------------------------------------------- Watchdog
#if PLATFORM_ESP32
void watchdogBegin(uint32_t timeoutMs) {
#if ESP_IDF_VERSION_MAJOR >= 5
    // Arduino core 3.x (pioarduino): TWDT уже создан ядром — перенастраиваем.
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = timeoutMs;
    cfg.idle_core_mask = 0;
    cfg.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
    // Arduino core 2.x (официальная платформа PlatformIO, IDF 4.4): таймаут в секундах,
    // повторный init обновляет таймаут и panic. Округляем вверх, чтобы не сократить таймаут.
    const uint32_t timeoutS = (timeoutMs + 999) / 1000;
    esp_task_wdt_init(timeoutS ? timeoutS : 1, true);
    // Как idle_core_mask = 0 в 3.x: следим только за задачей управления, а не за idle-задачами
    // (иначе долгий Wi-Fi/веб на ядре 0 перезагрузит контроллер без вины цикла управления).
    for (int core = 0; core < portNUM_PROCESSORS; ++core) {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core);
        if (idle) esp_task_wdt_delete(idle);  // ESP_ERR_* если не была подписана — не важно
    }
#endif
    esp_task_wdt_add(nullptr);  // подписываем текущую задачу (control)
}
void watchdogFeed() { esp_task_wdt_reset(); }
bool resetWasWatchdog() {
    const esp_reset_reason_t r = esp_reset_reason();
    return r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT || r == ESP_RST_PANIC;
}
void reboot() { ESP.restart(); }

const char* chipName() {
#if CONFIG_IDF_TARGET_ESP32S3
    return "ESP32-S3";
#elif CONFIG_IDF_TARGET_ESP32C3
    return "ESP32-C3";
#else
    return "ESP32";
#endif
}
uint32_t chipId() {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
}
#else
void watchdogBegin(uint32_t timeoutMs) { rp2040.wdt_begin(timeoutMs); }
void watchdogFeed() { rp2040.wdt_reset(); }
bool resetWasWatchdog() { return watchdog_enable_caused_reboot(); }
void reboot() { rp2040.reboot(); }
const char* chipName() {
#if defined(PICO_RP2350)
    return "RP2350";
#else
    return "RP2040";
#endif
}
uint32_t chipId() {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    return ((uint32_t)id.id[4] << 24) | ((uint32_t)id.id[5] << 16) | ((uint32_t)id.id[6] << 8) | id.id[7];
}
#endif

}  // namespace hal

#if PLATFORM_RP2
// Второе ядро RP2040/RP2350 целиком отдано циклу управления.
void setup1() {
    while (!hal::s_started) delay(1);
}
void loop1() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last >= hal::s_period) {
        last = now;
        hal::s_tick();
    }
    delay(1);
}
#endif
