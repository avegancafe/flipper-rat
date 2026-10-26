#include "motion_math.h"

#include <math.h>

RatVec3 rat_grip_remap(RatGrip grip, RatVec3 v) {
    if(grip == RatGripUpright) {
        /* +90 degrees about Y: x' = -z, z' = x. Direction errors are fixed with Invert X/Y. */
        RatVec3 r = {.x = -v.z, .y = v.y, .z = v.x};
        return r;
    }
    return v;
}

float rat_angle_diff(float a, float b) {
    float diff = a - b;
    while(diff > 180.f)
        diff -= 360.f;
    while(diff < -180.f)
        diff += 360.f;
    return diff;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int8_t rat_exp_rate(float v, float rate) {
    float mag = fabsf(clampf(v, -127.f, 127.f)) / 127.f;
    float out = powf(mag, rate) * 127.f;
    int8_t i = (int8_t)out;
    return (v < 0.f) ? (int8_t)-i : i;
}

void rat_tracker_reset(RatTracker* t) {
    t->has_last = false;
    t->last_yaw = 0.f;
    t->last_pitch = 0.f;
    t->acc_x = 0.f;
    t->acc_y = 0.f;
}

void rat_tracker_feed(
    RatTracker* t,
    const RatMotionConfig* cfg,
    float yaw,
    float pitch,
    bool active) {
    if(!isfinite(yaw) || !isfinite(pitch)) return;

    if(!t->has_last) {
        t->has_last = true;
        t->last_yaw = yaw;
        t->last_pitch = pitch;
        return;
    }

    float dx = rat_angle_diff(t->last_yaw, yaw) * cfg->sensitivity;
    float dy = rat_angle_diff(t->last_pitch, pitch) * cfg->sensitivity;
    t->last_yaw = yaw;
    t->last_pitch = pitch;

    if(!active) {
        t->acc_x = 0.f;
        t->acc_y = 0.f;
        return;
    }

    t->acc_x += cfg->invert_x ? -dx : dx;
    t->acc_y += cfg->invert_y ? -dy : dy;
}

bool rat_tracker_take(RatTracker* t, const RatMotionConfig* cfg, int8_t* dx, int8_t* dy) {
    float mx = clampf(t->acc_x, -127.f, 127.f);
    float my = clampf(t->acc_y, -127.f, 127.f);

    /* Remove the whole counts we are about to send; keep the fraction for next time. */
    t->acc_x -= (float)(int8_t)mx;
    t->acc_y -= (float)(int8_t)my;

    *dx = rat_exp_rate(mx, cfg->curve);
    *dy = rat_exp_rate(my, cfg->curve);
    return (*dx != 0) || (*dy != 0);
}

bool rat_calib_is_still(RatVec3 mean_dps, RatVec3 std_dps) {
    return fabsf(mean_dps.x) < RAT_CALIB_MAX_MEAN_DPS &&
           fabsf(mean_dps.y) < RAT_CALIB_MAX_MEAN_DPS &&
           fabsf(mean_dps.z) < RAT_CALIB_MAX_MEAN_DPS && std_dps.x < RAT_CALIB_MAX_STD_DPS &&
           std_dps.y < RAT_CALIB_MAX_STD_DPS && std_dps.z < RAT_CALIB_MAX_STD_DPS;
}

void rat_drift_reset(RatDriftComp* d) {
    d->bias = (RatVec3){0.f, 0.f, 0.f};
    d->still_count = 0;
}

RatVec3 rat_drift_apply(RatDriftComp* d, RatVec3 g) {
    RatVec3 c = {g.x - d->bias.x, g.y - d->bias.y, g.z - d->bias.z};
    bool still = fabsf(c.x) < RAT_STILL_DPS && fabsf(c.y) < RAT_STILL_DPS &&
                 fabsf(c.z) < RAT_STILL_DPS;

    if(!still) {
        d->still_count = 0;
        return c;
    }
    if(d->still_count < RAT_STILL_SAMPLES) {
        d->still_count++;
        return c;
    }

    d->bias.x = clampf(d->bias.x + RAT_DRIFT_ALPHA * c.x, -RAT_DRIFT_MAX_DPS, RAT_DRIFT_MAX_DPS);
    d->bias.y = clampf(d->bias.y + RAT_DRIFT_ALPHA * c.y, -RAT_DRIFT_MAX_DPS, RAT_DRIFT_MAX_DPS);
    d->bias.z = clampf(d->bias.z + RAT_DRIFT_ALPHA * c.z, -RAT_DRIFT_MAX_DPS, RAT_DRIFT_MAX_DPS);
    return (RatVec3){g.x - d->bias.x, g.y - d->bias.y, g.z - d->bias.z};
}
