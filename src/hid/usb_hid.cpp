#include "usb_hid.h"

#ifdef HID_BACKEND_USB

#include <Arduino.h>
#include <tusb.h>
#include "../keymap.h"

namespace hid {

// Construct the USB HID keyboard and mouse at global scope so their
// TinyUSB interface registration happens during C++ static initialization,
// before the Arduino core calls USB.begin() in app_main(). If we wait until
// setup() to create them, the USB stack is already initialized and HID is
// left out of the composite descriptor.
static USBHIDKeyboard g_keyboard;
static USBHIDMouse g_mouse;

UsbHidBackend::UsbHidBackend() : keyboard_(g_keyboard), mouse_(g_mouse) {}

void UsbHidBackend::set_identity(uint16_t vid, uint16_t pid,
                                  const std::string& manufacturer,
                                  const std::string& product) {
    vid_          = vid;
    pid_          = pid;
    manufacturer_ = manufacturer;
    product_      = product;
}

bool UsbHidBackend::begin() {
    // Apply USB device identity before starting the stack.
    // These must be set before USB.begin() so the host sees the correct values
    // during enumeration. Changes take effect on each reboot/re-enumeration.
    USB.VID(vid_);
    USB.PID(pid_);
    USB.manufacturerName(manufacturer_.c_str());
    USB.productName(product_.c_str());

    // The HID objects were already registered during static construction.
    // Now we just finish their setup and ensure the USB stack is started.
    keyboard_.begin();
    mouse_.begin();
    bool usb_started = USB.begin();
    Serial.printf("[USB] VID=%04X PID=%04X mfr='%s' prod='%s' begin=%s\n",
                  vid_, pid_, manufacturer_.c_str(), product_.c_str(),
                  usb_started ? "ok" : "already started");
    return true;
}

bool UsbHidBackend::is_connected() {
    // tud_mounted() is true once the host has enumerated the USB HID interface.
    return tud_mounted();
}

void UsbHidBackend::send_report(const keymap::Report& report) {
    KeyReport kr = {};
    kr.modifiers = report.modifiers;
    kr.keys[0] = report.keycode;
    keyboard_.sendReport(&kr);
    // Briefly hold the key so the host sees a real press, then release.
    vTaskDelay(1); // yield for one RTOS tick instead of busy-waiting
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
}

bool UsbHidBackend::send_char(char ch) {
    keymap::Report r = keymap::lookup(ch, layout_);
    if (!r.valid) {
        // Skip characters we don't know how to type.
        return false;
    }
    send_report(r);
    return true;
}

bool UsbHidBackend::send_string(const std::string& text) {
    bool ok = true;
    for (char ch : text) {
        if (!send_char(ch)) ok = false;
    }
    return ok;
}

bool UsbHidBackend::send_key(uint8_t keycode, uint8_t modifiers) {
    KeyReport kr = {};
    kr.modifiers = modifiers;
    kr.keys[0] = keycode;
    keyboard_.sendReport(&kr);
    vTaskDelay(1); // yield for one RTOS tick instead of busy-waiting
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
    return true;
}

void UsbHidBackend::release_all() {
    keyboard_.releaseAll();
}

void UsbHidBackend::set_layout(const std::string& layout) {
    if (keymap::is_supported(layout)) {
        layout_ = layout;
    }
}

bool UsbHidBackend::send_mouse_move(int8_t dx, int8_t dy, uint8_t buttons) {
    (void)buttons;
    mouse_.move(dx, dy);
    return true;
}

bool UsbHidBackend::send_mouse_button(uint8_t button, bool pressed) {
    if (pressed) {
        mouse_.press(button);
    } else {
        mouse_.release();
    }
    return true;
}

void UsbHidBackend::mouse_release_all() {
    mouse_.release();
}

IHidBackend* create_backend() {
    static UsbHidBackend instance;
    return &instance;
}

} // namespace hid

#endif // HID_BACKEND_USB
