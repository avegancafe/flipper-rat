#pragma once

/* Key-name lookup for profile chords. Pure C (no furi) so it builds on the host for tests.
 * Values are standard USB HID usages; modifier bits match KEY_MOD_* in furi_hal_usb_hid.h,
 * so a chord is a single uint16_t (modifiers in the high byte) accepted by kb_press/kb_release. */

#include <stdbool.h>
#include <stdint.h>

#define RAT_MOD_CTRL  (1u << 8)
#define RAT_MOD_SHIFT (1u << 9)
#define RAT_MOD_ALT   (1u << 10)
#define RAT_MOD_GUI   (1u << 11)

/* Look up a key name ("RIGHT", "F5", "B", "PAGEDOWN", "."). Case-insensitive.
 * `len` is the number of chars of `name` to consider. Returns false if unknown. */
bool rat_key_lookup(const char* name, unsigned len, uint8_t* out_usage);

/* Look up a modifier name ("CTRL", "CMD", "OPT", ...). Case-insensitive. */
bool rat_mod_lookup(const char* name, unsigned len, uint16_t* out_mod);
