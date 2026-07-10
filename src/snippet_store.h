// NVS-backed persistent snippet storage (up to MAX_SNIPPETS slots).
// Snippets survive reboots and are served via the /api/snippets REST API.
#pragma once

#include <string>
#include <vector>

namespace snippet {

static const int MAX_SNIPPETS = 8;

struct Snippet {
    int         id    = -1;   // -1 means this slot is empty
    std::string title;
    std::string text;
};

class Store {
public:
    Store() = default;

    // Load all slots from NVS. Call once at startup.
    void begin();

    // All occupied slots (id >= 0).
    std::vector<Snippet> list() const;

    // One slot by id (0–MAX_SNIPPETS-1). Returns id=-1 if empty.
    Snippet get(int id) const;

    // Save or overwrite a slot. Returns false if id is out of range.
    bool set(int id, const std::string& title, const std::string& text);

    // Clear a slot.
    void erase(int id);

private:
    Snippet cache_[MAX_SNIPPETS];

    void load(int id);
    void save(int id);
};

} // namespace snippet
