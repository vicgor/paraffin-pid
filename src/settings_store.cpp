#include "settings_store.h"

#include "hal.h"

#if PLATFORM_ESP32
#include <Preferences.h>
#else
#include <EEPROM.h>
#endif

namespace {

constexpr uint32_t kMagic = 0x50415246;  // 'PARF'
constexpr uint16_t kVersion = 1;

struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    AppSettings s;
    uint32_t crc;
};

// Один буфер на все операции: структура ~700 байт, на стеке обработчиков
// веба/CLI ей не место (#4). Доступ — только под g_storeLock: save() может
// прийти одновременно из веба, CLI и MQTT, и без замка во flash уйдёт
// смесь двух конфигураций или неверная CRC (#3).
// ВАЖНО: не вызывать из цикла управления — запись во flash и ожидание мьютекса
// нарушили бы детерминизм и могли бы уронить watchdog.
hal::Mutex g_storeLock;
Blob g_blob;

uint32_t crc32(const uint8_t* d, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *d++;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320 & (0u - (c & 1)));
    }
    return ~c;
}

uint32_t blobCrc(const Blob& b) { return crc32(reinterpret_cast<const uint8_t*>(&b), offsetof(Blob, crc)); }

bool readBlob(Blob& b) {
#if PLATFORM_ESP32
    Preferences p;
    if (!p.begin("paraffin", true)) return false;
    const size_t n = p.getBytes("cfg", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
#else
    EEPROM.begin(sizeof(Blob) < 4096 ? ((sizeof(Blob) + 255) / 256) * 256 : 4096);
    EEPROM.get(0, b);
    return true;
#endif
}

bool writeBlob(const Blob& b) {
#if PLATFORM_ESP32
    Preferences p;
    if (!p.begin("paraffin", false)) return false;
    const size_t n = p.putBytes("cfg", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
#else
    static_assert(sizeof(Blob) <= 4096, "settings too big for EEPROM emulation");
    EEPROM.begin(((sizeof(Blob) + 255) / 256) * 256);
    EEPROM.put(0, b);
    return EEPROM.commit();
#endif
}

}  // namespace

namespace settings_store {

bool load(AppSettings& s) {
    hal::LockGuard lk(g_storeLock);
    Blob& b = g_blob;
    memset((void*)&b, 0, sizeof(b));
    if (readBlob(b) && b.magic == kMagic && b.version == kVersion && b.size == sizeof(AppSettings) &&
        b.crc == blobCrc(b)) {
        s = b.s;
        ctl::sanitize(s.ctl);
        return true;
    }
    s = AppSettings();
    return false;
}

bool save(const AppSettings& s) {
    hal::LockGuard lk(g_storeLock);
    Blob& b = g_blob;
    memset((void*)&b, 0, sizeof(b));  // обнуляем padding, чтобы CRC была детерминированной
    b.magic = kMagic;
    b.version = kVersion;
    b.size = sizeof(AppSettings);
    b.s = s;
    b.crc = blobCrc(b);
    return writeBlob(b);
}

void factoryReset() {
    hal::LockGuard lk(g_storeLock);
#if PLATFORM_ESP32
    Preferences p;
    if (p.begin("paraffin", false)) {
        p.clear();
        p.end();
    }
#else
    // 0xFF — «стёртая» flash: magic не совпадёт, load() вернёт умолчания
    memset((void*)&g_blob, 0xFF, sizeof(g_blob));
    writeBlob(g_blob);
#endif
}

}  // namespace settings_store
