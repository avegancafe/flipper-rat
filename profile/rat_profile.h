#pragma once

/* Presenter profiles (one flipper_format text file per presentation tool) and app settings. */

#include <furi.h>
#include <storage/storage.h>
#include "../core/motion_math.h"
#include "../transport/rat_transport.h"

#define RAT_PROFILES_DIR      APP_DATA_PATH("profiles")
#define RAT_PROFILES_ASSETS   APP_ASSETS_PATH("profiles")
#define RAT_SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define RAT_PROFILE_FILETYPE  "flipper-rat profile"
#define RAT_SETTINGS_FILETYPE "flipper-rat settings"
#define RAT_DEFAULT_PROFILE   "generic.txt"

typedef enum {
    RatActionNext,
    RatActionPrev,
    RatActionStart,
    RatActionStop,
    RatActionBlack,
    RatActionPointer,
    RatActionCount,
} RatAction;

typedef struct {
    char name[32];
    char os[16];
    uint16_t chords[RatActionCount];
} RatProfile;

/* The built-in fallback (same as files/profiles/generic.txt). */
void rat_profile_set_generic(RatProfile* profile);

/* Loads `path`. On failure fills `error` with a short reason and leaves `profile` untouched.
 * Missing action keys mean "none"; an unparseable chord is an error. */
bool rat_profile_load(Storage* storage, const char* path, RatProfile* profile, FuriString* error);

/* Copies bundled default profiles into RAT_PROFILES_DIR unless a file of that name exists. */
void rat_profiles_install_defaults(Storage* storage);

typedef enum {
    RatSensitivityLow,
    RatSensitivityMedium,
    RatSensitivityHigh,
    RatSensitivityCount,
} RatSensitivity;

typedef struct {
    RatTransportType transport;
    RatSensitivity sensitivity;
    bool accel_curve;
    bool always_on; /* false = clutch (hold Right to move) */
    RatGrip grip;
    bool invert_x;
    bool invert_y;
    char profile_file[64]; /* file name inside RAT_PROFILES_DIR */
} RatSettings;

void rat_settings_defaults(RatSettings* settings);
/* Missing file or keys keep their defaults. */
void rat_settings_load(Storage* storage, RatSettings* settings);
bool rat_settings_save(Storage* storage, const RatSettings* settings);

/* Motion tuning derived from settings. */
RatMotionConfig rat_settings_motion_config(const RatSettings* settings);
