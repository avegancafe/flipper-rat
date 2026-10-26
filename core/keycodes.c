#include "keycodes.h"

typedef struct {
    const char* name;
    uint16_t code;
} RatKeyName;

static const RatKeyName mod_names[] = {
    {"CTRL", RAT_MOD_CTRL},
    {"CONTROL", RAT_MOD_CTRL},
    {"SHIFT", RAT_MOD_SHIFT},
    {"ALT", RAT_MOD_ALT},
    {"OPT", RAT_MOD_ALT},
    {"OPTION", RAT_MOD_ALT},
    {"GUI", RAT_MOD_GUI},
    {"CMD", RAT_MOD_GUI},
    {"COMMAND", RAT_MOD_GUI},
    {"WIN", RAT_MOD_GUI},
    {"WINDOWS", RAT_MOD_GUI},
    {"SUPER", RAT_MOD_GUI},
    {"META", RAT_MOD_GUI},
};

static const RatKeyName key_names[] = {
    {"ENTER", 0x28},       {"RETURN", 0x28},     {"ESC", 0x29},      {"ESCAPE", 0x29},
    {"BACKSPACE", 0x2A},   {"TAB", 0x2B},        {"SPACE", 0x2C},    {"CAPSLOCK", 0x39},
    {"F1", 0x3A},          {"F2", 0x3B},         {"F3", 0x3C},       {"F4", 0x3D},
    {"F5", 0x3E},          {"F6", 0x3F},         {"F7", 0x40},       {"F8", 0x41},
    {"F9", 0x42},          {"F10", 0x43},        {"F11", 0x44},      {"F12", 0x45},
    {"PRINTSCREEN", 0x46}, {"SCROLLLOCK", 0x47}, {"PAUSE", 0x48},    {"INSERT", 0x49},
    {"HOME", 0x4A},        {"PAGEUP", 0x4B},     {"PGUP", 0x4B},     {"DELETE", 0x4C},
    {"DEL", 0x4C},         {"END", 0x4D},        {"PAGEDOWN", 0x4E}, {"PGDN", 0x4E},
    {"RIGHT", 0x4F},       {"LEFT", 0x50},       {"DOWN", 0x51},     {"UP", 0x52},
    {"MENU", 0x65},        {"APP", 0x65},        {"MINUS", 0x2D},    {"EQUAL", 0x2E},
    {"PERIOD", 0x37},      {"COMMA", 0x36},      {"SLASH", 0x38},    {"SEMICOLON", 0x33},
};

/* Single printable characters (US layout, unshifted). */
static const char punct_chars[] = "-=[]\\;'`,./";
static const uint8_t punct_usages[] =
    {0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};

static char upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool name_eq(const char* table_name, const char* name, unsigned len) {
    unsigned i = 0;
    for(; i < len; i++) {
        if(table_name[i] == '\0' || table_name[i] != upper(name[i])) return false;
    }
    return table_name[i] == '\0';
}

bool rat_key_lookup(const char* name, unsigned len, uint8_t* out_usage) {
    if(!name || len == 0) return false;

    if(len == 1) {
        char c = upper(name[0]);
        if(c >= 'A' && c <= 'Z') {
            *out_usage = (uint8_t)(0x04 + (c - 'A'));
            return true;
        }
        if(c >= '1' && c <= '9') {
            *out_usage = (uint8_t)(0x1E + (c - '1'));
            return true;
        }
        if(c == '0') {
            *out_usage = 0x27;
            return true;
        }
        for(unsigned i = 0; punct_chars[i]; i++) {
            if(punct_chars[i] == c) {
                *out_usage = punct_usages[i];
                return true;
            }
        }
        return false;
    }

    for(unsigned i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if(name_eq(key_names[i].name, name, len)) {
            *out_usage = (uint8_t)key_names[i].code;
            return true;
        }
    }
    return false;
}

bool rat_mod_lookup(const char* name, unsigned len, uint16_t* out_mod) {
    if(!name || len == 0) return false;
    for(unsigned i = 0; i < sizeof(mod_names) / sizeof(mod_names[0]); i++) {
        if(name_eq(mod_names[i].name, name, len)) {
            *out_mod = mod_names[i].code;
            return true;
        }
    }
    return false;
}
