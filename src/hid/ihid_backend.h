// Abstract interface for keyboard output backends.
// Implementations: UsbHidBackend (ESP32-S3 USB-OTG) and BleHidBackend (ESP32 BLE).
#pragma once

#include <cstdint>
#include <string>

namespace hid {

class IHidBackend {
public:
    virtual ~IHidBackend() = default;

    // Initialize the backend (start USB, begin BLE advertising, etc.).
    virtual bool begin() = 0;

    // Returns true when the host is connected and ready to receive keystrokes.
    virtual bool is_connected() = 0;

    // Send a single ASCII/Unicode character. Returns true on success.
    virtual bool send_char(char ch) = 0;

    // Send a raw string. Returns true on success.
    virtual bool send_string(const std::string& text) = 0;

    // Send a key press/release for a named special key (ENTER, TAB, etc.).
    virtual bool send_key(uint8_t keycode, uint8_t modifiers = 0) = 0;

    // Release all keys.
    virtual void release_all() = 0;

    // Set the host keyboard layout the backend should emit for.
    virtual void set_layout(const std::string& layout) { (void)layout; }

    // ---------- mouse capabilities ----------

    // True if this backend can emit mouse HID reports.
    virtual bool has_mouse() const { return false; }

    // Send a relative mouse movement with optional button mask.
    // buttons: bit 0 = left, bit 1 = right, bit 2 = middle.
    virtual bool send_mouse_move(int8_t dx, int8_t dy, uint8_t buttons = 0) {
        (void)dx; (void)dy; (void)buttons;
        return false;
    }

    // Press or release a mouse button without moving.
    virtual bool send_mouse_button(uint8_t button, bool pressed) {
        (void)button; (void)pressed;
        return false;
    }

    // Release all mouse buttons.
    virtual void mouse_release_all() {}
};

// Factory: creates the backend selected at compile time.
IHidBackend* create_backend();

} // namespace hid
