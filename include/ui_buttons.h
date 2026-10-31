#pragma once
#include <stdint.h>

namespace uiinput {
enum class Event : uint8_t { None, Click, Long, Back };

class Buttons {
public:
    static constexpr uint8_t kEncoder = 1, kConfirm = 2, kBack = 4;
    static constexpr uint32_t kDebounceMs = 30, kLongMs = 1200;

    Event poll(uint8_t mask, uint32_t nowMs) {
        if (!initialized_ || mask != raw_) {
            initialized_ = true;
            raw_ = mask;
            changeMs_ = nowMs;
        }
        // Любая комбинация или смена кнопки без отпускания отменяет жест.
        if ((mask && (mask & (mask - 1))) || (owner_ && mask && mask != owner_)) {
            blocked_ = true;
            owner_ = 0;
        }
        if (nowMs - changeMs_ < kDebounceMs) return Event::None;
        if (blocked_) {
            if (!mask) blocked_ = false;
            return Event::None;
        }
        if (!mask) {
            const bool click = owner_ && !sent_;
            owner_ = 0;
            return click ? Event::Click : Event::None;
        }
        if (!owner_) {
            owner_ = mask;
            pressMs_ = nowMs;
            sent_ = mask == kBack;
            return sent_ ? Event::Back : Event::None;
        }
        if (!sent_ && nowMs - pressMs_ >= kLongMs) {
            sent_ = true;
            return Event::Long;
        }
        return Event::None;
    }
    bool held() const { return owner_ != 0; }

private:
    // После загрузки требуется устойчивое отпускание: зажатая кнопка не запускает нагрев.
    bool initialized_ = false, blocked_ = true, sent_ = false;
    uint8_t raw_ = 0, owner_ = 0;
    uint32_t changeMs_ = 0, pressMs_ = 0;
};

// BACK только меняет экран; не преобразуется в Long (на главном это запуск нагрева).
template <typename Screen>
Screen backScreen(Screen screen) {
    return screen == Screen::Main || screen == Screen::Menu ? Screen::Main : Screen::Menu;
}
}  // namespace uiinput
