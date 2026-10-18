#pragma once
// Выбор распиновки по целевому чипу. Любой пин можно переопределить через build_flags.
#if defined(BOARD_PINS_FILE)
#include BOARD_PINS_FILE
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "boards/board_esp32s3.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include "boards/board_rp2350.h"
#else
#error "Нет файла распиновки для этой платы — добавьте include/boards/board_xxx.h"
#endif

// Активный уровень SSR (1 — HIGH включает)
#ifndef SSR_ACTIVE_HIGH
#define SSR_ACTIVE_HIGH 1
#endif
// Импульсов энкодера на один щелчок (EC11 обычно 4)
#ifndef ENC_STEPS_PER_DETENT
#define ENC_STEPS_PER_DETENT 4
#endif
