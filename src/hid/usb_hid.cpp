#include "usb_hid.h"

#ifdef HID_BACKEND_USB

#include <Arduino.h>
#include <tusb.h>
#include "../keymap.h"

// ── RTC mode flag ─────────────────────────────────────────────────────────────
// Survives software resets (esp_restart()), cleared on power-off.
// setup() syncs NVS desired mode → this flag → restarts if different.
// Static init (before setup()) reads this to configure behaviour.
// bit 0: 1 = mouse-only  (keyboard HID reports suppressed)
// bit 1: 1 = serial mode (informational only — CDC always registered by framework)
RTC_DATA_ATTR int8_t g_rtc_usb_mode = 0;

// ── USB identity mirrors ──────────────────────────────────────────────────────
// Logitech K380 defaults — set before the very first USB enumeration.
const char* g_usb_manufacturer = "Logitech";
const char* g_usb_product      = "K380 Multi-Device Keyboard";
uint16_t    g_usb_vid          = 0x046D;
uint16_t    g_usb_pid          = 0xB342;

// ── Static HID objects ────────────────────────────────────────────────────────
// Both keyboard and mouse are always registered with TinyUSB at static-init
// time (before USB.begin()). In mouse-only mode we simply don't send
// keyboard HID reports — the interface remains in the descriptor.
// Full interface removal requires a configuration-descriptor override which
// can be implemented later without risking crashes.
static USBHIDKeyboard g_keyboard;
static USBHIDMouse    g_mouse;

namespace hid {

UsbHidBackend::UsbHidBackend()
    : keyboard_(g_keyboard), mouse_(g_mouse) {}

void UsbHidBackend::set_identity(uint16_t vid, uint16_t pid,
                                  const std::string& manufacturer,
                                  const std::string& product) {
    g_usb_vid = vid;
    g_usb_pid = pid;
    static std::string s_mfr, s_prod;
    s_mfr = manufacturer;
    s_prod = product;
    g_usb_manufacturer = s_mfr.c_str();
    g_usb_product      = s_prod.c_str();
}

bool UsbHidBackend::begin() {
    keyboard_.begin();
    mouse_.begin();
    bool ok = USB.begin();
    Serial.printf("[USB] mode=0x%02X (mouse_only=%d serial=%d) vid=%04X pid=%04X\n",
                  g_rtc_usb_mode, (g_rtc_usb_mode & 1) != 0, (g_rtc_usb_mode & 2) != 0,
                  g_usb_vid, g_usb_pid);
    return true;
}

bool UsbHidBackend::is_connected() {
    return tud_mounted();
}

void UsbHidBackend::send_report(const keymap::Report& report) {
    if (g_rtc_usb_mode & 1) return; // mouse-only: suppress keyboard
    KeyReport kr = {};
    kr.modifiers = report.modifiers;
    kr.keys[0]   = report.keycode;
    keyboard_.sendReport(&kr);
    vTaskDelay(1);
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
}

bool UsbHidBackend::send_char(char ch) {
    if (g_rtc_usb_mode & 1) return false;
    keymap::Report r = keymap::lookup(ch, layout_);
    if (!r.valid) return false;
    send_report(r);
    return true;
}

bool UsbHidBackend::send_string(const std::string& text) {
    if (g_rtc_usb_mode & 1) return false;
    bool ok = true;
    for (char ch : text) { if (!send_char(ch)) ok = false; }
    return ok;
}

bool UsbHidBackend::send_key(uint8_t keycode, uint8_t modifiers) {
    if (g_rtc_usb_mode & 1) return false;
    KeyReport kr = {};
    kr.modifiers = modifiers;
    kr.keys[0]   = keycode;
    keyboard_.sendReport(&kr);
    vTaskDelay(1);
    kr.keys[0] = 0;
    keyboard_.sendReport(&kr);
    return true;
}

void UsbHidBackend::release_all() {
    if (!(g_rtc_usb_mode & 1)) keyboard_.releaseAll();
}

void UsbHidBackend::set_layout(const std::string& layout) {
    if (keymap::is_supported(layout)) layout_ = layout;
}

bool UsbHidBackend::send_mouse_move(int8_t dx, int8_t dy, uint8_t buttons) {
    (void)buttons;
    mouse_.move(dx, dy);
    return true;
}

bool UsbHidBackend::send_mouse_button(uint8_t button, bool pressed) {
    if (pressed) mouse_.press(button);
    else         mouse_.release();
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
