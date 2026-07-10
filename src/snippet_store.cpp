#include "snippet_store.h"

#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif
#include <Preferences.h>

namespace snippet {

static const char* NS = "pd_snip";

// Key helpers — NVS limits keys to 15 chars.
static String title_key(int id) { return String("t") + String(id); }
static String text_key(int  id) { return String("x") + String(id); }
static String used_key(int  id) { return String("u") + String(id); }

void Store::begin() {
    for (int i = 0; i < MAX_SNIPPETS; ++i) load(i);
}

void Store::load(int id) {
    cache_[id].id = -1;
    Preferences prefs;
    if (!prefs.begin(NS, true)) return;
    if (!prefs.getBool(used_key(id).c_str(), false)) { prefs.end(); return; }
    cache_[id].id    = id;
    cache_[id].title = std::string(prefs.getString(title_key(id).c_str(), "").c_str());
    cache_[id].text  = std::string(prefs.getString(text_key(id).c_str(),  "").c_str());
    prefs.end();
}

void Store::save(int id) {
    Preferences prefs;
    if (!prefs.begin(NS, false)) return;
    if (cache_[id].id < 0) {
        prefs.remove(used_key(id).c_str());
        prefs.remove(title_key(id).c_str());
        prefs.remove(text_key(id).c_str());
    } else {
        prefs.putBool(used_key(id).c_str(),   true);
        prefs.putString(title_key(id).c_str(), cache_[id].title.c_str());
        prefs.putString(text_key(id).c_str(),  cache_[id].text.c_str());
    }
    prefs.end();
}

std::vector<Snippet> Store::list() const {
    std::vector<Snippet> out;
    for (int i = 0; i < MAX_SNIPPETS; ++i) {
        if (cache_[i].id >= 0) out.push_back(cache_[i]);
    }
    return out;
}

Snippet Store::get(int id) const {
    if (id < 0 || id >= MAX_SNIPPETS) return Snippet{};
    return cache_[id];
}

bool Store::set(int id, const std::string& title, const std::string& text) {
    if (id < 0 || id >= MAX_SNIPPETS) return false;
    cache_[id].id    = id;
    cache_[id].title = title;
    cache_[id].text  = text;
    save(id);
    return true;
}

void Store::erase(int id) {
    if (id < 0 || id >= MAX_SNIPPETS) return;
    cache_[id].id = -1;
    cache_[id].title.clear();
    cache_[id].text.clear();
    save(id);
}

} // namespace snippet
