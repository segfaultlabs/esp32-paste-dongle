// USB HID keyboard backend for ESP32-S3.
#pragma once

#include "ihid_backend.h"
#include "../keymap.h"

#ifdef HID_BACKEND_USB
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

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

private:
    USBHIDKeyboard& keyboard_;
    USBHIDMouse& mouse_;
    std::string layout_ = "US";

    void send_report(const keymap::Report& report);
};

} // namespace hid

#endif // HID_BACKEND_USB
