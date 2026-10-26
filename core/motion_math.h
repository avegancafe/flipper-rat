#pragma once

/* Orientation -> mouse-delta math. Pure C (only <math.h>) so it is unit-tested on the host.
 * Pipeline (per IMU sample, ~1 kHz):
 *   rat_grip_remap() on accel+gyro  ->  Madgwick filter (imu/)  ->  yaw/pitch in degrees
 *   rat_tracker_feed(yaw, pitch, active)   accumulates angle change * sensitivity while active
 *   rat_tracker_take() every N samples     clamps to +-127, applies accel curve, keeps the remainder */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RatGripFlat = 0, /* screen up, top edge pointing at the target (default) */
    RatGripUpright, /* screen facing you, top edge pointing at the ceiling */
} RatGrip;

typedef struct {
    float x, y, z;
} RatVec3;

/* Rotate a sensor-frame vector so an upright-held Flipper looks flat to the filter.
 * Flat is the identity. Upright rotates 90 degrees about the sensor Y (left/right) axis. */
RatVec3 rat_grip_remap(RatGrip grip, RatVec3 v);

/* Shortest signed difference a - b in degrees, wrapped to [-180, 180]. */
float rat_angle_diff(float a, float b);

/* Acceleration curve on a value in [-127, 127]: sign(v) * 127 * (|v|/127)^rate.
 * rate 1.0 is linear; >1 makes small motions finer. Result is truncated toward zero. */
int8_t rat_exp_rate(float v, float rate);

typedef struct {
    float sensitivity; /* counts per degree */
    float curve; /* rat_exp_rate exponent */
    bool invert_x;
    bool invert_y;
} RatMotionConfig;

typedef struct {
    bool has_last;
    float last_yaw;
    float last_pitch;
    float acc_x;
    float acc_y;
} RatTracker;

void rat_tracker_reset(RatTracker* t);

/* Feed one orientation sample. While `active` is false the tracker still follows the
 * orientation (so re-activating doesn't jump) but discards movement and any leftover fraction. */
void rat_tracker_feed(
    RatTracker* t,
    const RatMotionConfig* cfg,
    float yaw,
    float pitch,
    bool active);

/* Take accumulated movement as one HID report. Returns true if (dx, dy) is non-zero. */
bool rat_tracker_take(RatTracker* t, const RatMotionConfig* cfg, int8_t* dx, int8_t* dy);
