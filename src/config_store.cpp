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

static const char* NAMESPACE = "paste_dongle";
static const char* KEY_DEVICE_NAME = "device_name";
static const char* KEY_AP_SSID = "ap_ssid";
static const char* KEY_AP_PASS = "ap_pass";

static const char* KEY_JIGGLER_ENABLED = "j_en";
static const char* KEY_JIGGLER_INTERVAL = "j_iv";
static const char* KEY_JIGGLER_DISTANCE = "j_dist";
static const char* KEY_JIGGLER_PATTERN = "j_pat";
static const char* KEY_JIGGLER_RANDOMIZE = "j_rnd";
static const char* KEY_LAYOUT = "layout";

static const char* DEFAULT_AP_SSID = "PasteDongle";
static const char* DEFAULT_AP_PASS = "pastepaste";
static const char* DEFAULT_JIGGLER_PATTERN = "random";
static const char* DEFAULT_LAYOUT = "US";

bool Store::begin() {
    if (begun_) return true;
    begun_ = ensure_nvs();
    return begun_;
}

void Store::end() {
    begun_ = false;
}

std::string Store::get_device_name() {
    if (!ensure_nvs()) return "";
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return "";
    if (!prefs.isKey(KEY_DEVICE_NAME)) {
        prefs.end();
        return "";
    }
    String name = prefs.getString(KEY_DEVICE_NAME, "");
    prefs.end();
    return std::string(name.c_str());
}

void Store::set_device_name(const std::string& name) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    if (name.empty()) {
        prefs.remove(KEY_DEVICE_NAME);
    } else {
        prefs.putString(KEY_DEVICE_NAME, name.c_str());
    }
    prefs.end();
}

std::string Store::get_ap_ssid() {
    if (!ensure_nvs()) return DEFAULT_AP_SSID;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return DEFAULT_AP_SSID;
    String v = prefs.getString(KEY_AP_SSID, DEFAULT_AP_SSID);
    prefs.end();
    return std::string(v.c_str());
}

std::string Store::get_ap_password() {
    if (!ensure_nvs()) return DEFAULT_AP_PASS;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return DEFAULT_AP_PASS;
    String v = prefs.getString(KEY_AP_PASS, DEFAULT_AP_PASS);
    prefs.end();
    return std::string(v.c_str());
}

bool Store::get_jiggler_enabled() {
    if (!ensure_nvs()) return false;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return false;
    bool v = prefs.getBool(KEY_JIGGLER_ENABLED, false);
    prefs.end();
    return v;
}

void Store::set_jiggler_enabled(bool v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putBool(KEY_JIGGLER_ENABLED, v);
    prefs.end();
}

uint32_t Store::get_jiggler_interval_ms() {
    if (!ensure_nvs()) return 30000;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return 30000;
    uint32_t v = prefs.getUInt(KEY_JIGGLER_INTERVAL, 30000);
    prefs.end();
    return v;
}

void Store::set_jiggler_interval_ms(uint32_t v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putUInt(KEY_JIGGLER_INTERVAL, v);
    prefs.end();
}

int8_t Store::get_jiggler_distance() {
    if (!ensure_nvs()) return 2;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return 2;
    int v = prefs.getInt(KEY_JIGGLER_DISTANCE, 2);
    prefs.end();
    return static_cast<int8_t>(v);
}

void Store::set_jiggler_distance(int8_t v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putInt(KEY_JIGGLER_DISTANCE, v);
    prefs.end();
}

std::string Store::get_jiggler_pattern() {
    if (!ensure_nvs()) return DEFAULT_JIGGLER_PATTERN;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return DEFAULT_JIGGLER_PATTERN;
    String v = prefs.getString(KEY_JIGGLER_PATTERN, DEFAULT_JIGGLER_PATTERN);
    prefs.end();
    return std::string(v.c_str());
}

void Store::set_jiggler_pattern(const std::string& v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putString(KEY_JIGGLER_PATTERN, v.c_str());
    prefs.end();
}

bool Store::get_jiggler_randomize() {
    if (!ensure_nvs()) return false;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return false;
    bool v = prefs.getBool(KEY_JIGGLER_RANDOMIZE, false);
    prefs.end();
    return v;
}

void Store::set_jiggler_randomize(bool v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putBool(KEY_JIGGLER_RANDOMIZE, v);
    prefs.end();
}

std::string Store::get_layout() {
    if (!ensure_nvs()) return DEFAULT_LAYOUT;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return DEFAULT_LAYOUT;
    String v = prefs.getString(KEY_LAYOUT, DEFAULT_LAYOUT);
    prefs.end();
    return std::string(v.c_str());
}

void Store::set_layout(const std::string& v) {
    if (!ensure_nvs()) return;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.putString(KEY_LAYOUT, v.c_str());
    prefs.end();
}

} // namespace config
