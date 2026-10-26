#pragma once

/* Video Game Module motion sensor -> mouse movement.
 * RatImu owns the ICM-42688-P on the external SPI bus; RatMotion is a worker thread that
 * calibrates, runs the orientation filter and sends mouse moves while "active". */

#include <stdbool.h>
#include "../core/motion_math.h"
#include "../transport/rat_transport.h"

typedef struct RatImu RatImu;
typedef struct RatMotion RatMotion;

/* Returns NULL if the Video Game Module (motion sensor) isn't attached. */
RatImu* rat_imu_alloc(void);
void rat_imu_free(RatImu* imu);

typedef void (*RatMotionReadyCallback)(void* context);

/* Starts the worker: calibrates gyro bias (hold still ~0.5 s), then calls `ready_cb`
 * (from the worker thread). Movement is only sent while rat_motion_set_active(true). */
RatMotion* rat_motion_start(
    RatImu* imu,
    RatTransport* transport,
    const RatMotionConfig* config,
    RatGrip grip,
    RatMotionReadyCallback ready_cb,
    void* context);

void rat_motion_set_active(RatMotion* motion, bool active);

/* Ignore movement for the next `ms` milliseconds even while active (e.g. the wobble of
 * letting go of a button). */
void rat_motion_suppress(RatMotion* motion, uint32_t ms);

void rat_motion_stop(RatMotion* motion);
