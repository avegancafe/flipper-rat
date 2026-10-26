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
