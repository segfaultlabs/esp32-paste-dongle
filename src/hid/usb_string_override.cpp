// Override TinyUSB descriptor callbacks.
//
// Both tud_descriptor_device_cb and tud_descriptor_string_cb are declared
// __attribute__((weak)) in esp32-hal-tinyusb.c, so our definitions win.
//
// The framework's internal string table is static and inaccessible, so we
// serve strings directly from the g_usb_* globals set by set_identity().
// These globals are initialised to the Logitech K380 default so the VERY
// FIRST USB enumeration (before setup() runs) already shows the correct
// identity rather than "Espressif Systems".
//
// Device descriptor:
//   VID/PID   ← from g_usb_vid / g_usb_pid
//   iSerial = 0 (no serial number — suppresses the MAC-address fingerprint)
//
// String descriptor indices:
//   0  Language (English)
//   1  Manufacturer  ← g_usb_manufacturer ("Logitech")
//   2  Product       ← g_usb_product ("K380 Multi-Device Keyboard")
//   3  Serial        → nullptr (no serial number)
//   4  Interface 0   → empty string (hides "TinyUSB HID" / "TinyUSB CDC")
//   5+ Interface N   → nullptr
//
// After these overrides, ioreg / system_profiler show:
//   Vendor:  Logitech (or whatever preset is active)
//   Product: K380 Multi-Device Keyboard
//   VID/PID: 0x046D / 0xB342
//   No "TinyUSB" strings anywhere.

#ifdef HID_BACKEND_USB

#include <cstring>
#include <cstdint>
#include "tusb.h"

extern const char* g_usb_manufacturer;
extern const char* g_usb_product;
extern uint16_t    g_usb_vid;
extern uint16_t    g_usb_pid;

// ── Device descriptor ────────────────────────────────────────────────────────

extern "C" uint8_t const* tud_descriptor_device_cb(void) {
    static tusb_desc_device_t desc = {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = 0xEF,  // Misc (composite device)
        .bDeviceSubClass    = 0x02,
        .bDeviceProtocol    = 0x01,
        .bMaxPacketSize0    = 64,
        .idVendor           = 0x046D, // updated each call from global
        .idProduct          = 0xB342,
        .bcdDevice          = 0x0100,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x00,  // 0 = no serial number
        .bNumConfigurations = 0x01
    };
    desc.idVendor  = g_usb_vid;
    desc.idProduct = g_usb_pid;
    return (uint8_t const*)&desc;
}

// ── String descriptor ────────────────────────────────────────────────────────

static uint16_t s_str_buf[64];

static uint16_t const* make_str(const char* str) {
    if (!str) return nullptr;
    size_t len = strlen(str);
    if (len > 62) len = 62;
    for (size_t i = 0; i < len; i++)
        s_str_buf[1 + i] = (uint16_t)(unsigned char)str[i];
    s_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (uint8_t)(2 + 2 * len));
    return s_str_buf;
}

extern "C" uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    switch (index) {
        case 0:
            s_str_buf[0] = (TUSB_DESC_STRING << 8) | 4;
            s_str_buf[1] = 0x0409; // English
            return s_str_buf;
        case 1:  return make_str(g_usb_manufacturer);
        case 2:  return make_str(g_usb_product);
        case 3:  return nullptr; // no serial number
        case 4:  // HID / CDC interface string — return empty, not "TinyUSB HID"
            s_str_buf[0] = (TUSB_DESC_STRING << 8) | 2;
            return s_str_buf;
        default: return nullptr;
    }
}

#endif // HID_BACKEND_USB
