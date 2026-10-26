/* Motion worker. Sensor handling, calibration and the Madgwick filter are adapted from the
 * Video Game Module Air Mouse (flipperdevices/flipperzero-good-faps, GPL-3.0). */

#include "rat_motion.h"
#include "ICM42688P.h"

#include <furi.h>
#include <furi_hal.h>
#include <math.h>

#define TAG "RatMotion"

#define SAMPLE_RATE     DataRate1kHz
#define SAMPLE_FREQ     1000.f
#define FILTER_BETA     0.04f /* lower = less accelerometer (hand-jolt) noise in tilt */
#define CALI_SAMPLES    256
#define PITCH_LIMIT_DEG 75.f

typedef enum {
    FlagStop = (1 << 0),
    FlagData = (1 << 1),
} RatMotionFlags;

struct RatImu {
    FuriHalSpiBusHandle spi;
    ICM42688P* sensor;
};

typedef struct {
    float q0, q1, q2, q3;
} Quat;

struct RatMotion {
    FuriThread* thread;
    RatImu* imu;
    RatTransport* transport;
    RatMotionConfig config;
    RatGrip grip;
    RatMotionReadyCallback ready_cb;
    void* context;
    volatile bool active;
    volatile uint32_t suppress_until; /* furi tick */
    Quat q;
    RatTracker tracker;
    RatDriftComp drift;
};

RatImu* rat_imu_alloc(void) {
    RatImu* imu = malloc(sizeof(RatImu));
    memcpy(&imu->spi, &furi_hal_spi_bus_handle_external, sizeof(FuriHalSpiBusHandle));
    imu->spi.cs = &gpio_ext_pc3;
    imu->sensor = icm42688p_alloc(&imu->spi, &gpio_ext_pb2);
    if(!icm42688p_init(imu->sensor)) {
        FURI_LOG_W(TAG, "Motion sensor not found");
        icm42688p_deinit(imu->sensor); /* init already claimed the SPI pins */
        icm42688p_free(imu->sensor);
        free(imu);
        return NULL;
    }
    return imu;
}

void rat_imu_free(RatImu* imu) {
    if(!imu) return;
    if(!icm42688p_deinit(imu->sensor)) FURI_LOG_E(TAG, "Sensor deinit failed");
    icm42688p_free(imu->sensor);
    free(imu);
}

static void irq_cb(void* context) {
    RatMotion* m = context;
    furi_thread_flags_set(furi_thread_get_id(m->thread), FlagData);
}

#define CALI_MAX_TRIES 10 /* ~5 s; then take the stillest window and let drift correction fix it */

/* Measure the gyro's resting reading and load it into the sensor's offset registers.
 * Retries until a window looks still, so a hand still moving from the menu press doesn't
 * get baked in as bias. Returns false if a stop was requested meanwhile. */
static bool calibrate_gyro(ICM42688P* sensor) {
    ICM42688PRawData raw;
    ICM42688PScaledData zero = {0}, best = {0};
    float best_std = INFINITY;
    icm42688p_write_gyro_offset(sensor, &zero);
    furi_delay_ms(10);
    const float scale = icm42688p_gyro_get_full_scale(sensor) / 32768.f;

    for(int tries = 0; tries < CALI_MAX_TRIES; tries++) {
        float sx = 0, sy = 0, sz = 0, qx = 0, qy = 0, qz = 0;
        for(int i = 0; i < CALI_SAMPLES; i++) {
            icm42688p_read_gyro_raw(sensor, &raw);
            float x = raw.x * scale, y = raw.y * scale, z = raw.z * scale;
            sx += x;
            sy += y;
            sz += z;
            qx += x * x;
            qy += y * y;
            qz += z * z;
            furi_delay_ms(2);
        }
        if(furi_thread_flags_get() & FlagStop) return false;

        const float n = CALI_SAMPLES;
        RatVec3 mean = {sx / n, sy / n, sz / n};
        RatVec3 sd = {
            sqrtf(fmaxf(qx / n - mean.x * mean.x, 0.f)),
            sqrtf(fmaxf(qy / n - mean.y * mean.y, 0.f)),
            sqrtf(fmaxf(qz / n - mean.z * mean.z, 0.f)),
        };
        float worst = fmaxf(sd.x, fmaxf(sd.y, sd.z));
        if(worst < best_std) {
            best_std = worst;
            best = (ICM42688PScaledData){mean.x, mean.y, mean.z};
        }
        if(rat_calib_is_still(mean, sd)) {
            FURI_LOG_I(TAG, "Calibrated after %d tries, std %.2f dps", tries + 1, (double)worst);
            break;
        }
    }
    icm42688p_write_gyro_offset(sensor, &best);
    return true;
}

static float inv_sqrt(float x) {
    return 1.0f / sqrtf(x);
}

/* Madgwick AHRS (IMU variant), based on github.com/arduino-libraries/MadgwickAHRS. */
static void madgwick(Quat* q, RatVec3 a, RatVec3 g) {
    float q0 = q->q0, q1 = q->q1, q2 = q->q2, q3 = q->q3;
    float qd1 = 0.5f * (-q1 * g.x - q2 * g.y - q3 * g.z);
    float qd2 = 0.5f * (q0 * g.x + q2 * g.z - q3 * g.y);
    float qd3 = 0.5f * (q0 * g.y - q1 * g.z + q3 * g.x);
    float qd4 = 0.5f * (q0 * g.z + q1 * g.y - q2 * g.x);

    if(!(a.x == 0.f && a.y == 0.f && a.z == 0.f)) {
        float n = inv_sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        a.x *= n;
        a.y *= n;
        a.z *= n;

        float _2q0 = 2.f * q0, _2q1 = 2.f * q1, _2q2 = 2.f * q2, _2q3 = 2.f * q3;
        float _4q0 = 4.f * q0, _4q1 = 4.f * q1, _4q2 = 4.f * q2;
        float _8q1 = 8.f * q1, _8q2 = 8.f * q2;
        float q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;

        float s0 = _4q0 * q2q2 + _2q2 * a.x + _4q0 * q1q1 - _2q1 * a.y;
        float s1 = _4q1 * q3q3 - _2q3 * a.x + 4.f * q0q0 * q1 - _2q0 * a.y - _4q1 + _8q1 * q1q1 +
                   _8q1 * q2q2 + _4q1 * a.z;
        float s2 = 4.f * q0q0 * q2 + _2q0 * a.x + _4q2 * q3q3 - _2q3 * a.y - _4q2 + _8q2 * q1q1 +
                   _8q2 * q2q2 + _4q2 * a.z;
        float s3 = 4.f * q1q1 * q3 - _2q1 * a.x + 4.f * q2q2 * q3 - _2q2 * a.y;
        float sn = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
        if(sn > 0.f) {
            n = inv_sqrt(sn);
            qd1 -= FILTER_BETA * s0 * n;
            qd2 -= FILTER_BETA * s1 * n;
            qd3 -= FILTER_BETA * s2 * n;
            qd4 -= FILTER_BETA * s3 * n;
        }
    }

    q0 += qd1 / SAMPLE_FREQ;
    q1 += qd2 / SAMPLE_FREQ;
    q2 += qd3 / SAMPLE_FREQ;
    q3 += qd4 / SAMPLE_FREQ;
    float n = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q->q0 = q0 * n;
    q->q1 = q1 * n;
    q->q2 = q2 * n;
    q->q3 = q3 * n;
}

static void process_packet(
    RatMotion* m,
    ICM42688PFifoPacket* pkt,
    uint32_t* sample_cnt,
    uint32_t rate_div) {
    ICM42688PScaledData sa, sg;
    icm42688p_apply_scale_fifo(m->imu->sensor, pkt, &sa, &sg);

    const float d2r = (float)M_PI / 180.f;
    RatVec3 gd = rat_drift_apply(&m->drift, (RatVec3){sg.x, sg.y, sg.z});
    RatVec3 a = rat_grip_remap(m->grip, (RatVec3){sa.x, sa.y, sa.z});
    RatVec3 g = rat_grip_remap(m->grip, (RatVec3){gd.x * d2r, gd.y * d2r, gd.z * d2r});
    madgwick(&m->q, a, g);

    Quat* q = &m->q;
    float pitch = asinf(-2.0f * (q->q1 * q->q3 - q->q0 * q->q2)) / d2r;
    float yaw = atan2f(q->q1 * q->q2 + q->q0 * q->q3, 0.5f - q->q2 * q->q2 - q->q3 * q->q3) / d2r;

    /* Near vertical the yaw/pitch split is unstable; skip those samples. */
    if(!isfinite(pitch) || pitch < -PITCH_LIMIT_DEG || pitch > PITCH_LIMIT_DEG) return;

    /* Tilting the top edge up moves the cursor up ("point"); Invert Y gives flight-stick style. */
    bool active = m->active && (int32_t)(furi_get_tick() - m->suppress_until) >= 0;
    rat_tracker_feed(&m->tracker, &m->config, yaw, pitch, active);

    if(++(*sample_cnt) >= rate_div) {
        *sample_cnt = 0;
        int8_t dx, dy;
        if(rat_tracker_take(&m->tracker, &m->config, &dx, &dy) &&
           !rat_mouse_move(m->transport, dx, dy)) {
            rat_tracker_untake(&m->tracker); /* USB busy: send it with the next report */
        }
    }
}

static int32_t motion_thread(void* context) {
    RatMotion* m = context;
    ICM42688P* sensor = m->imu->sensor;

    if(!calibrate_gyro(sensor)) return 0;
    icm42688p_accel_config(sensor, AccelFullScale16G, SAMPLE_RATE);
    icm42688p_gyro_config(sensor, GyroFullScale2000DPS, SAMPLE_RATE);

    m->q = (Quat){1.f, 0.f, 0.f, 0.f};
    rat_tracker_reset(&m->tracker);
    rat_drift_reset(&m->drift);
    uint32_t rate_div = (uint32_t)SAMPLE_FREQ / rat_transport_report_rate(m->transport);
    m->config.report_hz = SAMPLE_FREQ / (float)rate_div;
    uint32_t sample_cnt = 0;

    icm42688_fifo_enable(sensor, irq_cb, m);
    if(m->ready_cb) m->ready_cb(m->context);

    while(true) {
        uint32_t flags =
            furi_thread_flags_wait(FlagStop | FlagData, FuriFlagWaitAny, FuriWaitForever);
        if(flags & FuriFlagError) continue;
        if(flags & FlagStop) break;
        if(flags & FlagData) {
            uint16_t pending = icm42688_fifo_get_count(sensor);
            ICM42688PFifoPacket pkt;
            while(pending--) {
                icm42688_fifo_read(sensor, &pkt);
                process_packet(m, &pkt, &sample_cnt, rate_div);
            }
        }
    }

    icm42688_fifo_disable(sensor);
    return 0;
}

RatMotion* rat_motion_start(
    RatImu* imu,
    RatTransport* transport,
    const RatMotionConfig* config,
    RatGrip grip,
    RatMotionReadyCallback ready_cb,
    void* context) {
    furi_check(imu && transport && config);
    RatMotion* m = malloc(sizeof(RatMotion));
    memset(m, 0, sizeof(RatMotion));
    m->imu = imu;
    m->transport = transport;
    m->config = *config;
    m->grip = grip;
    m->ready_cb = ready_cb;
    m->context = context;
    m->thread = furi_thread_alloc_ex("RatMotion", 4096, motion_thread, m);
    furi_thread_start(m->thread);
    return m;
}

void rat_motion_set_active(RatMotion* m, bool active) {
    m->active = active;
}

void rat_motion_suppress(RatMotion* m, uint32_t ms) {
    m->suppress_until = furi_get_tick() + furi_ms_to_ticks(ms);
}

void rat_motion_stop(RatMotion* m) {
    if(!m) return;
    furi_thread_flags_set(furi_thread_get_id(m->thread), FlagStop);
    furi_thread_join(m->thread);
    furi_thread_free(m->thread);
    free(m);
}
