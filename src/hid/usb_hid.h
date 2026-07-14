// USB HID keyboard + mouse backend for ESP32-S3.
// Mode switching (mouse-only) uses RTC_DATA_ATTR g_rtc_usb_mode.
// Both HID interfaces are always registered; mouse-only mode just suppresses
// keyboard reports rather than removing the interface (avoids crash risk).
#pragma once

#include "ihid_backend.h"
#include "../keymap.h"

#ifdef HID_BACKEND_USB
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

// USB mode bits stored in RTC memory (survives software resets, cleared on power-off).
// bit 0: 1 = mouse-only (keyboard reports suppressed)
// bit 1: 1 = serial enabled (informational; CDC always present via framework)
extern int8_t g_rtc_usb_mode;

namespace hid {

class UsbHidBackend : public IHidBackend {
public:
    UsbHidBackend();
    bool begin() override;
    bool is_connected() override;

    bool send_char(char ch) override;
    bool send_string(const std::string& text) override;
    bool send_key(uint8_t keycode, uint8_t modifiers = 0) override;
    void release_all() override;

    bool has_mouse() const override { return true; }
    bool send_mouse_move(int8_t dx, int8_t dy, uint8_t buttons = 0) override;
    bool send_mouse_button(uint8_t button, bool pressed) override;
    void mouse_release_all() override;

    void set_layout(const std::string& layout) override;
    void set_identity(uint16_t vid, uint16_t pid,
                      const std::string& manufacturer,
                      const std::string& product);

    static bool is_mouse_only()    { return (g_rtc_usb_mode & 1) != 0; }
    static bool is_serial_enabled(){ return (g_rtc_usb_mode & 2) != 0; }

private:
    USBHIDKeyboard& keyboard_;
    USBHIDMouse&    mouse_;
    std::string     layout_ = "US";

    void send_report(const keymap::Report& report);
};

} // namespace hid

#endif // HID_BACKEND_USB
