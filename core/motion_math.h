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
    /* Reports per second; enables smoothing. 0 sends everything immediately (no smoothing). */
    float report_hz;
} RatMotionConfig;

/* Smoothing: each report sends only part of the pending movement, which is a low-pass filter on
 * cursor speed (nothing is lost, it's just spread out). The cutoff rises with speed, so slow
 * aiming is steady and fast flicks stay snappy (the "1 euro filter" idea). */
#define RAT_SMOOTH_MIN_HZ 2.5f /* cutoff when nearly still */
#define RAT_SMOOTH_BETA   0.03f /* extra Hz per count/second of speed */

/* Fraction of the pending movement to send this report (1 = all). */
float rat_smooth_alpha(float pending_counts, float report_hz);

typedef struct {
    bool has_last;
    float last_yaw;
    float last_pitch;
    float acc_x; /* movement not sent yet, in counts */
    float acc_y;
    float taken_x; /* what the last take removed, for untake */
    float taken_y;
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

/* The last report couldn't be sent (radio busy): put its movement back so it goes out later. */
void rat_tracker_untake(RatTracker* t);

/* ---------- gyro bias (drift) ---------- */

/* Calibration window: the Flipper counts as still when every gyro axis has a standard
 * deviation below RAT_CALIB_MAX_STD_DPS and a mean below RAT_CALIB_MAX_MEAN_DPS (real sensor
 * bias is well under that; a bigger mean means the hand was turning). */
#define RAT_CALIB_MAX_STD_DPS  2.0f
#define RAT_CALIB_MAX_MEAN_DPS 4.0f
bool rat_calib_is_still(RatVec3 mean_dps, RatVec3 std_dps);

/* Running drift correction. After calibration a little bias usually remains (or the hand moved
 * slightly while calibrating). Whenever the corrected gyro stays within RAT_STILL_DPS on every
 * axis for RAT_STILL_SAMPLES samples, the bias estimate slowly follows the reading. */
#define RAT_STILL_DPS      1.5f
#define RAT_STILL_SAMPLES  500 /* 0.5 s at 1 kHz */
#define RAT_DRIFT_ALPHA    (1.f / 3000.f) /* ~3 s time constant at 1 kHz */
#define RAT_DRIFT_MAX_DPS  5.0f

typedef struct {
    RatVec3 bias; /* degrees per second */
    uint32_t still_count;
} RatDriftComp;

void rat_drift_reset(RatDriftComp* d);

/* Update the estimate with one gyro sample (deg/s) and return the corrected sample. */
RatVec3 rat_drift_apply(RatDriftComp* d, RatVec3 gyro_dps);
