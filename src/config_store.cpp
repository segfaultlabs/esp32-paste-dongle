#include "config_store.h"

#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif
#include <Preferences.h>
#include <nvs_flash.h>

namespace config {

static bool ensure_nvs() {
    static bool initialized = false;
    if (initialized) return true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.printf("[config] NVS init error %d, erasing and retrying\n", err);
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.printf("[config] NVS init failed: %d\n", err);
        return false;
    }
    initialized = true;
    return true;
}

static const char* NAMESPACE            = "paste_dongle";
static const char* KEY_DEVICE_NAME      = "device_name";
static const char* KEY_AP_SSID          = "ap_ssid";
static const char* KEY_AP_PASS          = "ap_pass";
static const char* KEY_JIGGLER_ENABLED  = "j_en";
static const char* KEY_JIGGLER_INTERVAL = "j_iv";
static const char* KEY_JIGGLER_DISTANCE = "j_dist";
static const char* KEY_JIGGLER_PATTERN  = "j_pat";
static const char* KEY_JIGGLER_RANDOMIZE = "j_rnd";
static const char* KEY_LAYOUT           = "layout";
static const char* KEY_JIGGLER_OU_RADIUS  = "j_ou_r";
static const char* KEY_JIGGLER_OU_JITTER  = "j_ou_j";
static const char* KEY_JIGGLER_OU_ANIM    = "j_ou_a";
static const char* KEY_USB_VID           = "usb_vid";
static const char* KEY_USB_PID           = "usb_pid";
static const char* KEY_USB_MFR           = "usb_mfr";
static const char* KEY_USB_PROD          = "usb_prd";
static const char* KEY_SIM_ENABLED      = "sim_en";
static const char* KEY_SIM_PAUSE        = "sim_pause";
static const char* KEY_SIM_WORDS        = "sim_words";

// Load everything in a single Preferences open so boot costs one NVS transaction.
void Store::load_all() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return;

    if (prefs.isKey(KEY_DEVICE_NAME))
        cache_.device_name = std::string(prefs.getString(KEY_DEVICE_NAME, "").c_str());
    if (prefs.isKey(KEY_AP_SSID))
        cache_.ap_ssid = std::string(prefs.getString(KEY_AP_SSID, cache_.ap_ssid.c_str()).c_str());
    if (prefs.isKey(KEY_AP_PASS))
        cache_.ap_password = std::string(prefs.getString(KEY_AP_PASS, cache_.ap_password.c_str()).c_str());

    cache_.jiggler_enabled     = prefs.getBool(KEY_JIGGLER_ENABLED,   false);
    cache_.jiggler_interval_ms = prefs.getUInt(KEY_JIGGLER_INTERVAL,  30000);
    cache_.jiggler_distance    = static_cast<int8_t>(prefs.getInt(KEY_JIGGLER_DISTANCE, 2));
    cache_.jiggler_pattern     = std::string(prefs.getString(KEY_JIGGLER_PATTERN, cache_.jiggler_pattern.c_str()).c_str());
    cache_.jiggler_randomize   = prefs.getBool(KEY_JIGGLER_RANDOMIZE, false);
    cache_.jiggler_ou_radius   = static_cast<uint8_t>(prefs.getUInt(KEY_JIGGLER_OU_RADIUS, 5));
    cache_.jiggler_ou_jitter   = static_cast<uint8_t>(prefs.getUInt(KEY_JIGGLER_OU_JITTER, 50));
    cache_.jiggler_ou_anim_ms  = static_cast<uint16_t>(prefs.getUInt(KEY_JIGGLER_OU_ANIM, 300));
    cache_.usb_vid             = prefs.getUInt(KEY_USB_VID, 0x046D);
    cache_.usb_pid             = prefs.getUInt(KEY_USB_PID, 0xB342);
    if (prefs.isKey(KEY_USB_MFR))
        cache_.usb_manufacturer = std::string(prefs.getString(KEY_USB_MFR, "Logitech").c_str());
    if (prefs.isKey(KEY_USB_PROD))
        cache_.usb_product = std::string(prefs.getString(KEY_USB_PROD, "K380 Multi-Device Keyboard").c_str());
    cache_.layout              = std::string(prefs.getString(KEY_LAYOUT, cache_.layout.c_str()).c_str());
    cache_.sim_enabled         = prefs.getBool(KEY_SIM_ENABLED,  false);
    cache_.sim_pause_ms        = prefs.getUInt(KEY_SIM_PAUSE,    18000);
    cache_.sim_words_burst     = static_cast<uint8_t>(prefs.getUInt(KEY_SIM_WORDS, 8));

    prefs.end();
}

bool Store::begin() {
    if (begun_) return true;
    if (!ensure_nvs()) return false;
    load_all();
    begun_ = true;
    return true;
}

void Store::end() {
    begun_ = false;
}

// Helper: open NVS for writing, apply a lambda, close.
#define NVS_WRITE(body)                                         \
    do {                                                        \
        if (!ensure_nvs()) return;                              \
        Preferences prefs;                                      \
        if (!prefs.begin(NAMESPACE, false)) return;            \
        body;                                                   \
        prefs.end();                                            \
    } while (0)

void Store::set_device_name(const std::string& v) {
    cache_.device_name = v;
    NVS_WRITE(
        if (v.empty()) prefs.remove(KEY_DEVICE_NAME);
        else           prefs.putString(KEY_DEVICE_NAME, v.c_str());
    );
}

void Store::set_jiggler_enabled(bool v) {
    cache_.jiggler_enabled = v;
    NVS_WRITE(prefs.putBool(KEY_JIGGLER_ENABLED, v));
}

void Store::set_jiggler_interval_ms(uint32_t v) {
    cache_.jiggler_interval_ms = v;
    NVS_WRITE(prefs.putUInt(KEY_JIGGLER_INTERVAL, v));
}

void Store::set_jiggler_distance(int8_t v) {
    cache_.jiggler_distance = v;
    NVS_WRITE(prefs.putInt(KEY_JIGGLER_DISTANCE, v));
}

void Store::set_jiggler_pattern(const std::string& v) {
    cache_.jiggler_pattern = v;
    NVS_WRITE(prefs.putString(KEY_JIGGLER_PATTERN, v.c_str()));
}

void Store::set_jiggler_randomize(bool v) {
    cache_.jiggler_randomize = v;
    NVS_WRITE(prefs.putBool(KEY_JIGGLER_RANDOMIZE, v));
}

void Store::set_layout(const std::string& v) {
    cache_.layout = v;
    NVS_WRITE(prefs.putString(KEY_LAYOUT, v.c_str()));
}

void Store::set_jiggler_ou_radius(uint8_t v) {
    cache_.jiggler_ou_radius = v;
    NVS_WRITE(prefs.putUInt(KEY_JIGGLER_OU_RADIUS, v));
}

void Store::set_jiggler_ou_jitter(uint8_t v) {
    cache_.jiggler_ou_jitter = v;
    NVS_WRITE(prefs.putUInt(KEY_JIGGLER_OU_JITTER, v));
}

void Store::set_jiggler_ou_anim_ms(uint16_t v) {
    cache_.jiggler_ou_anim_ms = v;
    NVS_WRITE(prefs.putUInt(KEY_JIGGLER_OU_ANIM, v));
}

void Store::set_usb_vid(uint32_t v) {
    cache_.usb_vid = v;
    NVS_WRITE(prefs.putUInt(KEY_USB_VID, v));
}
void Store::set_usb_pid(uint32_t v) {
    cache_.usb_pid = v;
    NVS_WRITE(prefs.putUInt(KEY_USB_PID, v));
}
void Store::set_usb_manufacturer(const std::string& v) {
    cache_.usb_manufacturer = v;
    NVS_WRITE(prefs.putString(KEY_USB_MFR, v.c_str()));
}
void Store::set_usb_product(const std::string& v) {
    cache_.usb_product = v;
    NVS_WRITE(prefs.putString(KEY_USB_PROD, v.c_str()));
}

void Store::set_sim_enabled(bool v) {
    cache_.sim_enabled = v;
    NVS_WRITE(prefs.putBool(KEY_SIM_ENABLED, v));
}

void Store::set_sim_pause_ms(uint32_t v) {
    cache_.sim_pause_ms = v;
    NVS_WRITE(prefs.putUInt(KEY_SIM_PAUSE, v));
}

void Store::set_sim_words_burst(uint8_t v) {
    cache_.sim_words_burst = v;
    NVS_WRITE(prefs.putUInt(KEY_SIM_WORDS, v));
}

} // namespace config
