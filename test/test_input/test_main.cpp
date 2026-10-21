#include <unity.h>
#include <initializer_list>
#include "../../include/ui_buttons.h"
using uiinput::Buttons;
using uiinput::Event;
void setUp() {}
void tearDown() {}
static void eq(Event expected, Event actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}
static void arm(Buttons& b, uint32_t t = 0) {
    eq(Event::None, b.poll(0, t));
    eq(Event::None, b.poll(0, t + 30));
}
void test_click_and_long() {
    for (uint8_t pin : {Buttons::kEncoder, Buttons::kConfirm}) {
        Buttons b;
        arm(b);
        eq(Event::None, b.poll(pin, 100));
        eq(Event::None, b.poll(pin, 130));
        eq(Event::None, b.poll(0, 200));
        eq(Event::Click, b.poll(0, 230));
        eq(Event::None, b.poll(0, 300));
        b.poll(pin, 400);
        b.poll(pin, 430);
        eq(Event::None, b.poll(pin, 1629));
        eq(Event::Long, b.poll(pin, 1630));
        eq(Event::None, b.poll(pin, 3000));
        b.poll(0, 3100);
        eq(Event::None, b.poll(0, 3130));
    }
}
void test_boot_held() {
    for (uint8_t pin : {Buttons::kEncoder, Buttons::kConfirm, Buttons::kBack}) {
        Buttons b;
        eq(Event::None, b.poll(pin, 0));
        eq(Event::None, b.poll(pin, 5000));
        eq(Event::None, b.poll(0, 5010));
        eq(Event::None, b.poll(0, 5040));
        b.poll(Buttons::kConfirm, 5100);
        b.poll(Buttons::kConfirm, 5130);
        b.poll(0, 5200);
        eq(Event::Click, b.poll(0, 5230));
    }
}
void test_back_never_click_or_long() {
    Buttons b;
    arm(b);
    b.poll(Buttons::kBack, 100);
    eq(Event::Back, b.poll(Buttons::kBack, 130));
    eq(Event::None, b.poll(Buttons::kBack, 5000));
    b.poll(0, 5100);
    eq(Event::None, b.poll(0, 5130));
}
void test_chord_and_partial_release() {
    for (uint8_t mask : {uint8_t(3), uint8_t(5), uint8_t(6), uint8_t(7)}) {
        Buttons b;
        arm(b);
        b.poll(Buttons::kConfirm, 100);
        b.poll(Buttons::kConfirm, 130);
        eq(Event::None, b.poll(mask, 1330));
        eq(Event::None, b.poll(Buttons::kConfirm, 1400));
        eq(Event::None, b.poll(Buttons::kConfirm, 5000));
        b.poll(0, 5100);
        eq(Event::None, b.poll(0, 5130));
        b.poll(Buttons::kConfirm, 5200);
        b.poll(Buttons::kConfirm, 5230);
        eq(Event::Long, b.poll(Buttons::kConfirm, 6430));
    }
}
void test_bounce() {
    Buttons b;
    arm(b);
    b.poll(Buttons::kConfirm, 100);
    b.poll(0, 110);
    b.poll(Buttons::kConfirm, 120);
    eq(Event::None, b.poll(Buttons::kConfirm, 149));
    b.poll(Buttons::kConfirm, 150);
    b.poll(0, 200);
    b.poll(Buttons::kConfirm, 210);
    b.poll(0, 220);
    eq(Event::None, b.poll(0, 249));
    eq(Event::Click, b.poll(0, 250));
}
void test_wraparound() {
    Buttons b;
    const uint32_t t = UINT32_MAX - 200;
    arm(b, t);
    b.poll(Buttons::kConfirm, t + 100);
    b.poll(Buttons::kConfirm, t + 130);
    eq(Event::Long, b.poll(Buttons::kConfirm, t + 1330));
}
void test_back_navigation() {
    enum class Screen { Main, Menu, Edit, Sensors, Network };
    TEST_ASSERT_TRUE(uiinput::backScreen(Screen::Main) == Screen::Main);
    TEST_ASSERT_TRUE(uiinput::backScreen(Screen::Menu) == Screen::Main);
    TEST_ASSERT_TRUE(uiinput::backScreen(Screen::Edit) == Screen::Menu);
    TEST_ASSERT_TRUE(uiinput::backScreen(Screen::Sensors) == Screen::Menu);
    TEST_ASSERT_TRUE(uiinput::backScreen(Screen::Network) == Screen::Menu);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_click_and_long);
    RUN_TEST(test_boot_held);
    RUN_TEST(test_back_never_click_or_long);
    RUN_TEST(test_chord_and_partial_release);
    RUN_TEST(test_bounce);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_back_navigation);
    return UNITY_END();
}
