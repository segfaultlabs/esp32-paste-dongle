#include "ble_hid.h"

#ifdef HID_BACKEND_BLE

#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif
#include "../config_store.h"

namespace hid {

BleHidBackend::BleHidBackend() {}

BleHidBackend::~BleHidBackend() {
    delete keyboard_;
}

bool BleHidBackend::begin() {
    config::Store cfg;
    std::string name = cfg.get_device_name();
    if (name.empty()) {
        // Use compile-time default when nothing is stored yet.
        name = DEVICE_NAME;
    }

    Serial.print("[BLE] advertising as: ");
    Serial.println(name.c_str());

    // BleKeyboard(name, manufacturer, batteryLevel)
    keyboard_ = new BleKeyboard(name.c_str(), "Espressif", 100);
    keyboard_->begin();
    return true;
}

bool BleHidBackend::is_connected() {
    if (!keyboard_) return false;
    return keyboard_->isConnected();
}

bool BleHidBackend::send_char(char ch) {
    if (!is_connected()) return false;
    keyboard_->write(ch);
    return true;
}

bool BleHidBackend::send_string(const std::string& text) {
    if (!is_connected()) return false;
    keyboard_->print(text.c_str());
    return true;
}

bool BleHidBackend::send_key(uint8_t keycode, uint8_t modifiers) {
    if (!is_connected()) return false;
    (void)modifiers;
    switch (keycode) {
        case 0x28: keyboard_->write(KEY_RETURN); break;
        case 0x2B: keyboard_->write(KEY_TAB); break;
        case 0x2C: keyboard_->write(' '); break;
        case 0x29: keyboard_->write(KEY_ESC); break;
        case 0x2A: keyboard_->write(KEY_BACKSPACE); break;
        default: keyboard_->write(static_cast<uint8_t>(keycode)); break;
    }
    return true;
}

void BleHidBackend::release_all() {
    // BleKeyboard manages key releases internally.
}

IHidBackend* create_backend() {
    static BleHidBackend instance;
    return &instance;
}

} // namespace hid

#endif // HID_BACKEND_BLE
