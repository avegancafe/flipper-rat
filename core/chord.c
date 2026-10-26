#include "chord.h"
#include "keycodes.h"

#include <string.h>

static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

static bool part_is(const char* s, unsigned len, const char* word) {
    unsigned n = (unsigned)strlen(word);
    if(len != n) return false;
    for(unsigned i = 0; i < n; i++) {
        char c = s[i];
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if(c != word[i]) return false;
    }
    return true;
}

bool rat_chord_parse(const char* text, uint16_t* out_chord) {
    if(!text || !out_chord) return false;

    const char* p = text;
    while(is_space(*p))
        p++;
    const char* end = p + strlen(p);
    while(end > p && is_space(end[-1]))
        end--;
    if(p == end) return false;

    if(part_is(p, (unsigned)(end - p), "none")) {
        *out_chord = RAT_CHORD_NONE;
        return true;
    }

    uint16_t mods = 0;
    bool have_key = false;
    uint8_t key = 0;

    while(p < end) {
        const char* q = p;
        while(q < end && *q != '+')
            q++;

        const char* ps = p;
        const char* pe = q;
        while(ps < pe && is_space(*ps))
            ps++;
        while(pe > ps && is_space(pe[-1]))
            pe--;
        unsigned len = (unsigned)(pe - ps);
        if(len == 0) return false;

        bool last = (q >= end);
        uint16_t mod;
        uint8_t usage;
        if(!last && rat_mod_lookup(ps, len, &mod)) {
            mods |= mod;
        } else if(last && rat_key_lookup(ps, len, &usage)) {
            key = usage;
            have_key = true;
        } else if(last && rat_mod_lookup(ps, len, &mod)) {
            /* Bare modifier as the key, e.g. "SHIFT" alone: not supported. */
            return false;
        } else {
            return false;
        }

        if(last) break;
        p = q + 1; /* skip the '+' separator */
        if(p >= end) return false; /* trailing '+' with nothing after */
    }

    if(!have_key) return false;
    *out_chord = (uint16_t)(mods | key);
    return true;
}
