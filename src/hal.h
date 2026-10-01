#pragma once
// Тонкий слой абстракции платформы: мьютекс, сторожевой таймер,
// запуск высокоприоритетного цикла управления, сведения о чипе.
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#define PLATFORM_ESP32 1
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#define ISR_ATTR IRAM_ATTR
#elif defined(ARDUINO_ARCH_RP2040)  // arduino-pico: RP2040 и RP2350
#define PLATFORM_RP2 1
#include <pico/mutex.h>
#define ISR_ATTR
#else
#error "Поддерживаются только ESP32 (Arduino core 3.x) и RP2040/RP2350 (arduino-pico)"
#endif

#ifndef HAS_WIFI
#if defined(PLATFORM_ESP32) || defined(PICO_CYW43_SUPPORTED)
#define HAS_WIFI 1
#else
#define HAS_WIFI 0
#endif
#endif

namespace hal {

class Mutex {
public:
    Mutex();
    void lock();
    void unlock();

private:
#if PLATFORM_ESP32
    SemaphoreHandle_t h_;
#else
    mutex_t m_;
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

using TickFn = void (*)();

// Запустить fn() в бесконечном цикле с периодом ~periodMs в отдельном контексте:
//  ESP32  — задача FreeRTOS с приоритетом выше loop();
//  RP2350 — второе ядро (loop1).
void startControlLoop(TickFn fn, uint32_t periodMs);

void watchdogBegin(uint32_t timeoutMs);  // вызывать из контекста цикла управления
void watchdogFeed();
bool resetWasWatchdog();
void reboot();

const char* chipName();
uint32_t chipId();  // уникальный идентификатор (младшие 32 бита)

}  // namespace hal
