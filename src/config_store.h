// Persistent configuration stored in NVS (Preferences).
#pragma once

#include <string>

#ifndef DEVICE_NAME
#define DEVICE_NAME "ESP32 Paste Dongle"
#endif

namespace config {

constexpr const char* DEFAULT_DEVICE_NAME = DEVICE_NAME;

class Store {
public:
    bool begin();
    void end();

    std::string get_device_name();
    void set_device_name(const std::string& name);

    std::string get_ap_ssid();
    std::string get_ap_password();

    // Mouse jiggler settings.
    bool get_jiggler_enabled();
    void set_jiggler_enabled(bool v);

    uint32_t get_jiggler_interval_ms();
    void set_jiggler_interval_ms(uint32_t v);

    int8_t get_jiggler_distance();
    void set_jiggler_distance(int8_t v);

    std::string get_jiggler_pattern();
    void set_jiggler_pattern(const std::string& v);

    bool get_jiggler_randomize();
    void set_jiggler_randomize(bool v);

    std::string get_layout();
    void set_layout(const std::string& v);

private:
    bool begun_ = false;
};

} // namespace config
