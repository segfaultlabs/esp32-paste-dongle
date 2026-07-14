#include "usb_hid.h"

#ifdef HID_BACKEND_USB

#include <Arduino.h>
#include <tusb.h>
#include "../keymap.h"

namespace hid {

// USB HID objects are constructed at global scope so TinyUSB registers their
// interfaces during C++ static init, before the Arduino core calls USB.begin().
// In HID_MOUSE_ONLY mode the keyboard object is omitted entirely so the USB
// descriptor contains only the mouse interface — the device presents to the host
// as a pure mouse with no keyboard capability.
#ifndef HID_MOUSE_ONLY
static USBHIDKeyboard g_keyboard;
#endif
static USBHIDMouse g_mouse;

UsbHidBackend::UsbHidBackend()
#ifndef HID_MOUSE_ONLY
    : keyboard_(g_keyboard), mouse_(g_mouse)
#else
    : mouse_(g_mouse)
#endif
{}

// Mirror of the active USB identity, readable by usb_string_override.cpp.
// Initialised to the default preset so the VERY FIRST USB enumeration (which
// happens before setup() runs) already presents as the correct device rather
// than as Espressif. set_identity() updates these from NVS in setup().
extern "C" {
    const char* g_usb_manufacturer = "Logitech";
    const char* g_usb_product      = "K380 Multi-Device Keyboard";
    uint16_t    g_usb_vid          = 0x046D;
    uint16_t    g_usb_pid          = 0xB342;
}

void UsbHidBackend::set_identity(uint16_t vid, uint16_t pid,
                                  const std::string& manufacturer,
                                  const std::string& product) {
    vid_          = vid;
    pid_          = pid;
    manufacturer_ = manufacturer;
    product_      = product;
    // Keep the mirrors in sync so the descriptor callbacks see fresh values.
    g_usb_vid          = vid;
    g_usb_pid          = pid;
    g_usb_manufacturer = manufacturer_.c_str();
    g_usb_product      = product_.c_str();
    // Also push to the Arduino USB layer (effective on next USB.begin()).
    USB.VID(vid);
    USB.PID(pid);
    USB.manufacturerName(manufacturer_.c_str());
    USB.productName(product_.c_str());
}

bool UsbHidBackend::begin() {
    // Begin registered HID interfaces. In HID_MOUSE_ONLY mode, only mouse is registered.
#ifndef HID_MOUSE_ONLY
    keyboard_.begin();
#endif
    mouse_.begin();
    bool usb_started = USB.begin();
    Serial.printf("[USB] VID=%04X PID=%04X mfr='%s' prod='%s' mode=%s begin=%s\n",
                  vid_, pid_, manufacturer_.c_str(), product_.c_str(),
#ifdef HID_MOUSE_ONLY
                  "mouse-only",
#else
                  "composite",
#endif
                  usb_started ? "ok" : "already started");
    return true;
}

bool UsbHidBackend::is_connected() {
    // tud_mounted() is true once the host has enumerated the USB HID interface.
    return tud_mounted();
}

void UsbHidBackend::send_report(const keymap::Report& report) {
#ifndef HID_MOUSE_ONLY
    KeyReport kr = {};
    kr.modifiers = report.modifiers;
    kr.keys[0] = report.keycode;
    keyboard_.sendReport(&kr);
    vTaskDelay(1);
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
#else
    (void)report;
#endif
}

bool UsbHidBackend::send_char(char ch) {
#ifdef HID_MOUSE_ONLY
    (void)ch; return false; // no keyboard interface in mouse-only mode
#else
    keymap::Report r = keymap::lookup(ch, layout_);
    if (!r.valid) return false;
    send_report(r);
    return true;
#endif
}

bool UsbHidBackend::send_string(const std::string& text) {
#ifdef HID_MOUSE_ONLY
    (void)text; return false;
#else
    bool ok = true;
    for (char ch : text) { if (!send_char(ch)) ok = false; }
    return ok;
#endif
}

bool UsbHidBackend::send_key(uint8_t keycode, uint8_t modifiers) {
#ifdef HID_MOUSE_ONLY
    (void)keycode; (void)modifiers; return false;
#else
    KeyReport kr = {};
    kr.modifiers = modifiers;
    kr.keys[0] = keycode;
    keyboard_.sendReport(&kr);
    vTaskDelay(1);
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
    return true;
#endif
}

void UsbHidBackend::release_all() {
#ifndef HID_MOUSE_ONLY
    keyboard_.releaseAll();
#endif
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
