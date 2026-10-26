#include "rat_profile.h"
#include "../core/chord.h"

#include <flipper_format/flipper_format.h>

#define TAG "RatProfile"

static const char* const action_keys[RatActionCount] = {
    [RatActionNext] = "next",
    [RatActionPrev] = "prev",
    [RatActionStart] = "start",
    [RatActionStop] = "stop",
    [RatActionBlack] = "black",
    [RatActionPointer] = "pointer",
};

void rat_profile_set_generic(RatProfile* p) {
    memset(p, 0, sizeof(RatProfile));
    strlcpy(p->name, "Generic", sizeof(p->name));
    strlcpy(p->os, "Any", sizeof(p->os));
    rat_chord_parse("RIGHT", &p->chords[RatActionNext]);
    rat_chord_parse("LEFT", &p->chords[RatActionPrev]);
    rat_chord_parse("F5", &p->chords[RatActionStart]);
    rat_chord_parse("ESC", &p->chords[RatActionStop]);
    rat_chord_parse("B", &p->chords[RatActionBlack]);
    p->chords[RatActionPointer] = RAT_CHORD_NONE;
}

bool rat_profile_load(Storage* storage, const char* path, RatProfile* out, FuriString* error) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    RatProfile p;
    memset(&p, 0, sizeof(p));
    bool ok = false;

    do {
        if(!flipper_format_file_open_existing(ff, path)) {
            furi_string_set(error, "Can't open file");
            break;
        }
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, str, &version) ||
           furi_string_cmp_str(str, RAT_PROFILE_FILETYPE) != 0) {
            furi_string_set(error, "Bad Filetype header");
            break;
        }
        if(version != 1) {
            furi_string_printf(error, "Unsupported Version %lu", version);
            break;
        }
        if(!flipper_format_read_string(ff, "Name", str)) {
            furi_string_set(error, "Missing Name");
            break;
        }
        strlcpy(p.name, furi_string_get_cstr(str), sizeof(p.name));

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "OS", str)) {
            strlcpy(p.os, furi_string_get_cstr(str), sizeof(p.os));
        }

        bool chords_ok = true;
        for(int a = 0; a < RatActionCount; a++) {
            flipper_format_rewind(ff);
            if(!flipper_format_read_string(ff, action_keys[a], str)) {
                p.chords[a] = RAT_CHORD_NONE;
                continue;
            }
            if(!rat_chord_parse(furi_string_get_cstr(str), &p.chords[a])) {
                furi_string_printf(
                    error, "%s: bad keys \"%s\"", action_keys[a], furi_string_get_cstr(str));
                chords_ok = false;
                break;
            }
        }
        if(!chords_ok) break;

        *out = p;
        ok = true;
    } while(false);

    if(!ok) FURI_LOG_W(TAG, "%s: %s", path, furi_string_get_cstr(error));
    furi_string_free(str);
    flipper_format_free(ff);
    return ok;
}

void rat_profiles_install_defaults(Storage* storage) {
    storage_simply_mkdir(storage, RAT_PROFILES_DIR);

    File* dir = storage_file_alloc(storage);
    FuriString* src = furi_string_alloc();
    FuriString* dst = furi_string_alloc();
    char name[64];

    if(storage_dir_open(dir, RAT_PROFILES_ASSETS)) {
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info)) continue;
            furi_string_printf(src, "%s/%s", RAT_PROFILES_ASSETS, name);
            furi_string_printf(dst, "%s/%s", RAT_PROFILES_DIR, name);
            if(!storage_common_exists(storage, furi_string_get_cstr(dst))) {
                FS_Error err = storage_common_copy(
                    storage, furi_string_get_cstr(src), furi_string_get_cstr(dst));
                if(err != FSE_OK) FURI_LOG_W(TAG, "Copy %s failed: %d", name, err);
            }
        }
    }
    storage_dir_close(dir);

    furi_string_free(dst);
    furi_string_free(src);
    storage_file_free(dir);
}

void rat_settings_defaults(RatSettings* s) {
    memset(s, 0, sizeof(RatSettings));
    s->transport = RatTransportBle;
    s->sensitivity = RatSensitivityMedium;
    s->accel_curve = true;
    s->always_on = false;
    s->grip = RatGripFlat;
    strlcpy(s->profile_file, RAT_DEFAULT_PROFILE, sizeof(s->profile_file));
}

static uint32_t read_u32(FlipperFormat* ff, const char* key, uint32_t def, uint32_t max) {
    uint32_t v = def;
    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, key, &v, 1) || v > max) v = def;
    return v;
}

void rat_settings_load(Storage* storage, RatSettings* s) {
    rat_settings_defaults(s);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    uint32_t version = 0;

    if(flipper_format_file_open_existing(ff, RAT_SETTINGS_PATH) &&
       flipper_format_read_header(ff, str, &version) &&
       furi_string_cmp_str(str, RAT_SETTINGS_FILETYPE) == 0 && version == 1) {
        s->transport = read_u32(ff, "Transport", s->transport, RatTransportUsb);
        s->sensitivity = read_u32(ff, "Sensitivity", s->sensitivity, RatSensitivityCount - 1);
        s->accel_curve = read_u32(ff, "AccelCurve", s->accel_curve, 1);
        s->always_on = read_u32(ff, "AlwaysOn", s->always_on, 1);
        s->grip = read_u32(ff, "Grip", s->grip, RatGripUpright);
        s->invert_x = read_u32(ff, "InvertX", s->invert_x, 1);
        s->invert_y = read_u32(ff, "InvertY", s->invert_y, 1);
        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Profile", str) && !furi_string_empty(str)) {
            strlcpy(s->profile_file, furi_string_get_cstr(str), sizeof(s->profile_file));
        }
    }

    furi_string_free(str);
    flipper_format_free(ff);
}

static bool write_u32(FlipperFormat* ff, const char* key, uint32_t v) {
    return flipper_format_write_uint32(ff, key, &v, 1);
}

bool rat_settings_save(Storage* storage, const RatSettings* s) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = flipper_format_file_open_always(ff, RAT_SETTINGS_PATH) &&
              flipper_format_write_header_cstr(ff, RAT_SETTINGS_FILETYPE, 1) &&
              write_u32(ff, "Transport", s->transport) &&
              write_u32(ff, "Sensitivity", s->sensitivity) &&
              write_u32(ff, "AccelCurve", s->accel_curve) &&
              write_u32(ff, "AlwaysOn", s->always_on) && write_u32(ff, "Grip", s->grip) &&
              write_u32(ff, "InvertX", s->invert_x) && write_u32(ff, "InvertY", s->invert_y) &&
              flipper_format_write_string_cstr(ff, "Profile", s->profile_file);
    if(!ok) FURI_LOG_E(TAG, "Saving settings failed");
    flipper_format_free(ff);
    return ok;
}

RatMotionConfig rat_settings_motion_config(const RatSettings* s) {
    static const float sens[RatSensitivityCount] = {15.f, 30.f, 50.f};
    RatMotionConfig c = {
        .sensitivity = sens[s->sensitivity],
        .curve = s->accel_curve ? 1.25f : 1.0f,
        .invert_x = s->invert_x,
        .invert_y = s->invert_y,
    };
    return c;
}
