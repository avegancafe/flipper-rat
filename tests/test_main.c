/* Host-side unit tests for the pure-C core. Run with `make test`. */
#include "../core/chord.h"
#include "../core/keycodes.h"
#include "../core/motion_math.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if(!(cond)) {                                              \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while(0)

static uint16_t chord(const char* s) {
    uint16_t c = 0xFFFF;
    CHECK(rat_chord_parse(s, &c));
    return c;
}

static void test_keycodes(void) {
    uint8_t u;
    CHECK(rat_key_lookup("a", 1, &u) && u == 0x04);
    CHECK(rat_key_lookup("Z", 1, &u) && u == 0x1D);
    CHECK(rat_key_lookup("1", 1, &u) && u == 0x1E);
    CHECK(rat_key_lookup("0", 1, &u) && u == 0x27);
    CHECK(rat_key_lookup(".", 1, &u) && u == 0x37);
    CHECK(rat_key_lookup("right", 5, &u) && u == 0x4F);
    CHECK(rat_key_lookup("PageDown", 8, &u) && u == 0x4E);
    CHECK(rat_key_lookup("F5", 2, &u) && u == 0x3E);
    CHECK(rat_key_lookup("F12", 3, &u) && u == 0x45);
    CHECK(rat_key_lookup("ESC", 3, &u) && u == 0x29);
    CHECK(rat_key_lookup("RIGHTX", 5, &u) && u == 0x4F); /* len limits the compare */
    CHECK(!rat_key_lookup("RIGH", 4, &u));
    CHECK(!rat_key_lookup("F13", 3, &u));
    CHECK(!rat_key_lookup("", 0, &u));

    uint16_t m;
    CHECK(rat_mod_lookup("cmd", 3, &m) && m == RAT_MOD_GUI);
    CHECK(rat_mod_lookup("Option", 6, &m) && m == RAT_MOD_ALT);
    CHECK(rat_mod_lookup("CTRL", 4, &m) && m == RAT_MOD_CTRL);
    CHECK(!rat_mod_lookup("HYPER", 5, &m));
}

static void test_chords(void) {
    CHECK(chord("RIGHT") == 0x4F);
    CHECK(chord("  left ") == 0x50);
    CHECK(chord("B") == 0x05);
    CHECK(chord("none") == RAT_CHORD_NONE);
    CHECK(chord("NONE") == RAT_CHORD_NONE);
    CHECK(chord("CMD+ALT+P") == (RAT_MOD_GUI | RAT_MOD_ALT | 0x13));
    CHECK(chord("cmd + enter") == (RAT_MOD_GUI | 0x28));
    CHECK(chord("Ctrl+Shift+F5") == (RAT_MOD_CTRL | RAT_MOD_SHIFT | 0x3E));
    CHECK(chord("SHIFT+F5") == (RAT_MOD_SHIFT | 0x3E));

    uint16_t c;
    CHECK(!rat_chord_parse("", &c));
    CHECK(!rat_chord_parse("   ", &c));
    CHECK(!rat_chord_parse("CMD+", &c));
    CHECK(!rat_chord_parse("+P", &c));
    CHECK(!rat_chord_parse("CMD++P", &c));
    CHECK(!rat_chord_parse("CMD", &c)); /* modifier alone */
    CHECK(!rat_chord_parse("P+CMD", &c)); /* key must be last */
    CHECK(!rat_chord_parse("A+B", &c)); /* only one key */
    CHECK(!rat_chord_parse("BOGUS", &c));
    CHECK(!rat_chord_parse(NULL, &c));
}

static RatMotionConfig cfg(float sens, float curve) {
    RatMotionConfig c = {
        .sensitivity = sens, .curve = curve, .invert_x = false, .invert_y = false};
    return c;
}

static void test_angle_and_curve(void) {
    CHECK(fabsf(rat_angle_diff(10.f, 5.f) - 5.f) < 1e-4f);
    CHECK(fabsf(rat_angle_diff(179.f, -179.f) - (-2.f)) < 1e-4f);
    CHECK(fabsf(rat_angle_diff(-179.f, 179.f) - 2.f) < 1e-4f);

    CHECK(rat_exp_rate(0.f, 1.3f) == 0);
    CHECK(rat_exp_rate(127.f, 1.3f) == 127);
    CHECK(rat_exp_rate(-127.f, 1.3f) == -127);
    CHECK(rat_exp_rate(500.f, 1.0f) == 127);
    CHECK(rat_exp_rate(50.f, 1.0f) == 50);
    CHECK(rat_exp_rate(-50.f, 1.0f) == -50);
    CHECK(rat_exp_rate(50.f, 1.3f) < 50); /* curve makes small moves finer */
    CHECK(rat_exp_rate(-50.f, 1.3f) == -rat_exp_rate(50.f, 1.3f));
}

static void test_tracker(void) {
    RatMotionConfig c = cfg(10.f, 1.0f);
    RatTracker t;
    int8_t dx, dy;

    /* First sample only primes the tracker. */
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &c, 0.f, 0.f, true);
    CHECK(!rat_tracker_take(&t, &c, &dx, &dy));

    /* last - current: yaw going down by 1 degree = +10 counts at sensitivity 10. */
    rat_tracker_feed(&t, &c, -1.f, 0.5f, true);
    CHECK(rat_tracker_take(&t, &c, &dx, &dy));
    CHECK(dx == 10 && dy == -5);

    /* Fractions carry over between reports. */
    rat_tracker_feed(&t, &c, -1.05f, 0.5f, true); /* +0.5 */
    CHECK(!rat_tracker_take(&t, &c, &dx, &dy));
    rat_tracker_feed(&t, &c, -1.1f, 0.5f, true); /* +0.5 -> 1.0 */
    CHECK(rat_tracker_take(&t, &c, &dx, &dy) && dx == 1 && dy == 0);

    /* Big moves clamp to 127 per report and the rest comes out next report. */
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &c, 0.f, 0.f, true);
    rat_tracker_feed(&t, &c, -20.f, 0.f, true); /* 200 counts */
    CHECK(rat_tracker_take(&t, &c, &dx, &dy) && dx == 127);
    CHECK(rat_tracker_take(&t, &c, &dx, &dy) && dx == 73);
    CHECK(!rat_tracker_take(&t, &c, &dx, &dy));

    /* Inactive (clutch released): follows orientation, drops movement and leftover fraction. */
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &c, 0.f, 0.f, true);
    rat_tracker_feed(&t, &c, -0.05f, 0.f, true); /* 0.5 pending */
    rat_tracker_feed(&t, &c, -30.f, 0.f, false); /* moved while inactive */
    CHECK(t.acc_x == 0.f);
    rat_tracker_feed(&t, &c, -30.05f, 0.f, true); /* re-activate: only the new 0.5 */
    CHECK(!rat_tracker_take(&t, &c, &dx, &dy));
    CHECK(fabsf(t.acc_x - 0.5f) < 1e-3f);

    /* Wrap-around at +-180 doesn't produce a huge jump. */
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &c, 179.9f, 0.f, true);
    rat_tracker_feed(&t, &c, -179.9f, 0.f, true);
    CHECK(rat_tracker_take(&t, &c, &dx, &dy) && dx == -2);

    /* Invert flags. */
    RatMotionConfig inv = c;
    inv.invert_x = true;
    inv.invert_y = true;
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &inv, 0.f, 0.f, true);
    rat_tracker_feed(&t, &inv, -1.f, 1.f, true);
    CHECK(rat_tracker_take(&t, &inv, &dx, &dy) && dx == -10 && dy == 10);

    /* NaN samples are ignored. */
    rat_tracker_reset(&t);
    rat_tracker_feed(&t, &c, 0.f, 0.f, true);
    rat_tracker_feed(&t, &c, NAN, 0.f, true);
    CHECK(!rat_tracker_take(&t, &c, &dx, &dy));
}

static void test_grip(void) {
    RatVec3 v = {.x = 1.f, .y = 2.f, .z = 3.f};
    RatVec3 f = rat_grip_remap(RatGripFlat, v);
    CHECK(f.x == 1.f && f.y == 2.f && f.z == 3.f);

    /* Upright: gravity along the sensor X axis moves onto Z (looks like lying flat). */
    RatVec3 g = {.x = -1.f, .y = 0.f, .z = 0.f};
    RatVec3 u = rat_grip_remap(RatGripUpright, g);
    CHECK(u.x == 0.f && u.y == 0.f && u.z == -1.f);
    /* Length is preserved. */
    RatVec3 r = rat_grip_remap(RatGripUpright, v);
    CHECK(fabsf(r.x * r.x + r.y * r.y + r.z * r.z - 14.f) < 1e-4f);
}

/* Every bundled profile must have a Name and parseable chords for the known actions. */
static void test_default_profiles(void) {
    static const char* const actions[] = {"next", "prev", "pointer"};
    DIR* d = opendir("files/profiles");
    CHECK(d != NULL);
    if(!d) return;
    int files = 0;
    struct dirent* e;
    while((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if(n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
        char path[300];
        snprintf(path, sizeof(path), "files/profiles/%s", e->d_name);
        FILE* f = fopen(path, "r");
        CHECK(f != NULL);
        if(!f) continue;
        files++;
        char line[200];
        int found = 0;
        int has_name = 0;
        int header = 0;
        while(fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if(strcmp(line, "Filetype: flipper-rat profile") == 0) header = 1;
            if(strncmp(line, "Name: ", 6) == 0) has_name = 1;
            for(unsigned a = 0; a < sizeof(actions) / sizeof(actions[0]); a++) {
                size_t k = strlen(actions[a]);
                if(strncmp(line, actions[a], k) == 0 && line[k] == ':') {
                    uint16_t c;
                    int ok = rat_chord_parse(line + k + 1, &c);
                    if(!ok) printf("  %s: %s\n", e->d_name, line);
                    CHECK(ok);
                    found++;
                }
            }
        }
        fclose(f);
        CHECK(header && has_name);
        CHECK(found == 3);
    }
    closedir(d);
    CHECK(files >= 7);
}

static void test_drift(void) {
    /* Calibration stillness check. */
    CHECK(rat_calib_is_still((RatVec3){0.3f, -0.2f, 0.1f}, (RatVec3){0.5f, 0.6f, 0.4f}));
    CHECK(!rat_calib_is_still((RatVec3){6.f, 0.f, 0.f}, (RatVec3){0.5f, 0.5f, 0.5f}));
    CHECK(!rat_calib_is_still((RatVec3){0.f, 0.f, 0.f}, (RatVec3){0.5f, 3.f, 0.5f}));

    /* A constant leftover bias while still is learned away (10 s at 1 kHz). */
    RatDriftComp d;
    rat_drift_reset(&d);
    RatVec3 out = {0};
    for(int i = 0; i < 10000; i++) out = rat_drift_apply(&d, (RatVec3){0.8f, -0.5f, 0.3f});
    CHECK(fabsf(out.x) < 0.05f && fabsf(out.y) < 0.05f && fabsf(out.z) < 0.05f);
    CHECK(fabsf(d.bias.x - 0.8f) < 0.05f);

    /* Nothing is learned during the first 0.5 s of stillness. */
    rat_drift_reset(&d);
    for(int i = 0; i < RAT_STILL_SAMPLES; i++) rat_drift_apply(&d, (RatVec3){1.f, 0.f, 0.f});
    CHECK(d.bias.x == 0.f);

    /* Real motion is passed through and never learned; it also restarts the still timer. */
    rat_drift_reset(&d);
    for(int i = 0; i < 5000; i++) out = rat_drift_apply(&d, (RatVec3){30.f, 0.f, 0.f});
    CHECK(d.bias.x == 0.f && out.x == 30.f);
    CHECK(d.still_count == 0);

    /* Learned bias is subtracted from later motion. */
    rat_drift_reset(&d);
    for(int i = 0; i < 20000; i++) rat_drift_apply(&d, (RatVec3){0.f, 0.f, 1.f});
    out = rat_drift_apply(&d, (RatVec3){0.f, 0.f, 21.f});
    CHECK(fabsf(out.z - 20.f) < 0.05f);
}

int main(void) {
    test_keycodes();
    test_chords();
    test_angle_and_curve();
    test_tracker();
    test_grip();
    test_drift();
    test_default_profiles();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
