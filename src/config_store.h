// Persistent configuration stored in NVS (Preferences).
// All values are loaded into RAM on begin(); getters read from cache (no NVS
// open/close per call). Setters update the cache and write to NVS in one
// Preferences transaction.
#pragma once

#include <string>
#include <cstdint>

#ifndef DEVICE_NAME
#define DEVICE_NAME "ESP32 Paste Dongle"
#endif

namespace config {

constexpr const char* DEFAULT_DEVICE_NAME = DEVICE_NAME;

struct CachedSettings {
    std::string device_name;
    std::string ap_ssid     = "PasteDongle";
    std::string ap_password = "pastepaste";
    bool        jiggler_enabled      = false;
    uint32_t    jiggler_interval_ms  = 30000;
    int8_t      jiggler_distance     = 2;
    std::string jiggler_pattern      = "natural";
    bool        jiggler_randomize    = false;
    uint8_t     jiggler_ou_radius    = 5;
    uint8_t     jiggler_ou_jitter    = 50;
    uint16_t    jiggler_ou_anim_ms   = 300;
    std::string layout               = "US";
    bool        sim_enabled          = false;
    uint32_t    sim_pause_ms         = 18000;
    uint8_t     sim_words_burst      = 8;
    // USB HID mode (runtime-switchable via RTC memory + reboot).
    bool        hid_mouse_only       = false; // false = composite, true = mouse-only
    bool        hid_serial_enabled   = false; // false = no CDC, true = CDC serial visible
    // USB HID identity (USB-only; requires reboot to apply).
    // Defaults to Logitech K380 so the device never advertises as Espressif.
    uint32_t    usb_vid              = 0x046D; // Logitech
    uint32_t    usb_pid              = 0xB342; // K380 Multi-Device Keyboard
    std::string usb_manufacturer     = "Logitech";
    std::string usb_product          = "K380 Multi-Device Keyboard";
};

class Store {
public:
    bool begin();
    void end();

    // Getters — all read from the in-RAM cache; no NVS overhead.
    const std::string& get_device_name()  const { return cache_.device_name; }
    const std::string& get_ap_ssid()      const { return cache_.ap_ssid; }
    const std::string& get_ap_password()  const { return cache_.ap_password; }
    bool               get_jiggler_enabled()     const { return cache_.jiggler_enabled; }
    uint32_t           get_jiggler_interval_ms() const { return cache_.jiggler_interval_ms; }
    int8_t             get_jiggler_distance()    const { return cache_.jiggler_distance; }
    const std::string& get_jiggler_pattern()     const { return cache_.jiggler_pattern; }
    bool               get_jiggler_randomize()   const { return cache_.jiggler_randomize; }
    uint8_t            get_jiggler_ou_radius()   const { return cache_.jiggler_ou_radius; }
    uint8_t            get_jiggler_ou_jitter()   const { return cache_.jiggler_ou_jitter; }
    uint16_t           get_jiggler_ou_anim_ms()  const { return cache_.jiggler_ou_anim_ms; }
    bool               get_hid_mouse_only()       const { return cache_.hid_mouse_only; }
    bool               get_hid_serial_enabled()   const { return cache_.hid_serial_enabled; }
    uint32_t           get_usb_vid()             const { return cache_.usb_vid; }
    uint32_t           get_usb_pid()             const { return cache_.usb_pid; }
    const std::string& get_usb_manufacturer()    const { return cache_.usb_manufacturer; }
    const std::string& get_usb_product()         const { return cache_.usb_product; }
    const std::string& get_layout()              const { return cache_.layout; }
    bool               get_sim_enabled()          const { return cache_.sim_enabled; }
    uint32_t           get_sim_pause_ms()         const { return cache_.sim_pause_ms; }
    uint8_t            get_sim_words_burst()      const { return cache_.sim_words_burst; }

    // Setters — update cache and persist to NVS.
    void set_device_name(const std::string& v);
    void set_jiggler_enabled(bool v);
    void set_jiggler_interval_ms(uint32_t v);
    void set_jiggler_distance(int8_t v);
    void set_jiggler_pattern(const std::string& v);
    void set_jiggler_randomize(bool v);
    void set_jiggler_ou_radius(uint8_t v);
    void set_jiggler_ou_jitter(uint8_t v);
    void set_jiggler_ou_anim_ms(uint16_t v);
    void set_hid_mouse_only(bool v);
    void set_hid_serial_enabled(bool v);
    void set_usb_vid(uint32_t v);
    void set_usb_pid(uint32_t v);
    void set_usb_manufacturer(const std::string& v);
    void set_usb_product(const std::string& v);
    void set_layout(const std::string& v);
    void set_sim_enabled(bool v);
    void set_sim_pause_ms(uint32_t v);
    void set_sim_words_burst(uint8_t v);

private:
    bool           begun_ = false;
    CachedSettings cache_;

    void load_all();
};

} // namespace config
