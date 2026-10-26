#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A chord is modifiers (high byte, KEY_MOD_* bits) | HID usage (low byte).
 * RAT_CHORD_NONE means "this action does nothing" (profile value `none`). */
#define RAT_CHORD_NONE 0u

/* Parse "CMD+ALT+P", "right", "Shift+F5", "none". Whitespace around parts is ignored.
 * Exactly one non-modifier key is required (except for "none").
 * Returns false on syntax errors or unknown names. */
bool rat_chord_parse(const char* text, uint16_t* out_chord);
