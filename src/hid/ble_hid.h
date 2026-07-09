// BLE HID keyboard backend for classic ESP32 (and ESP32-S3 in BLE mode).
#pragma once

#include "ihid_backend.h"

#ifdef HID_BACKEND_BLE
#include <BleKeyboard.h>

namespace hid {

class BleHidBackend : public IHidBackend {
public:
    BleHidBackend();
    ~BleHidBackend();

    bool begin() override;
    bool is_connected() override;
    bool send_char(char ch) override;
    bool send_string(const std::string& text) override;
    bool send_key(uint8_t keycode, uint8_t modifiers = 0) override;
    void release_all() override;

private:
    BleKeyboard* keyboard_ = nullptr;
};

} // namespace hid

#endif // HID_BACKEND_BLE
