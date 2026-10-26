#include "rat_transport.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_hid.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <storage/storage.h>

#define TAG "RatTransport"

#define RAT_BT_KEYS_PATH APP_DATA_PATH(".bt_hid.keys")
#define RAT_TAP_MS       20

/* Own name prefix and MAC so pairings don't collide with the stock Remote (0) or Air Mouse (1). */
static const BleProfileHidParams rat_ble_params = {
    .device_name_prefix = "Rat",
    .mac_xor = 0x5241,
};

struct RatTransport {
    RatTransportType type;
    FuriMutex* mutex;
    volatile bool connected;
    RatTransportStatusCallback status_cb;
    void* context;

    /* USB */
    FuriHalUsbInterface* usb_prev;
    FuriTimer* usb_poll;

    /* BLE */
    Bt* bt;
    FuriHalBleProfileBase* ble;
};

static void rat_set_connected(RatTransport* t, bool connected) {
    t->connected = connected;
    if(t->status_cb) t->status_cb(connected, t->context);
}

/* The HAL's USB state callback runs in the USB interrupt, where the GUI can't be notified.
 * Poll the connection flag from the timer thread instead. */
static void rat_usb_poll_cb(void* context) {
    RatTransport* t = context;
    bool connected = furi_hal_hid_is_connected();
    if(connected != t->connected) rat_set_connected(t, connected);
}

static void rat_bt_status_cb(BtStatus status, void* context) {
    rat_set_connected(context, status == BtStatusConnected);
}

RatTransport* rat_transport_start(
    RatTransportType type,
    RatTransportStatusCallback status_cb,
    void* context) {
    RatTransport* t = malloc(sizeof(RatTransport));
    memset(t, 0, sizeof(RatTransport));
    t->type = type;
    t->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    t->status_cb = status_cb;
    t->context = context;

    if(type == RatTransportUsb) {
        t->usb_prev = furi_hal_usb_get_config();
        furi_hal_usb_unlock();
        furi_check(furi_hal_usb_set_config(&usb_hid, NULL));
        t->connected = furi_hal_hid_is_connected();
        t->usb_poll = furi_timer_alloc(rat_usb_poll_cb, FuriTimerTypePeriodic, t);
        furi_timer_start(t->usb_poll, furi_ms_to_ticks(250));
    } else {
        t->bt = furi_record_open(RECORD_BT);
        bt_disconnect(t->bt);
        furi_delay_ms(200); /* let the radio core flush its NVM */
        bt_keys_storage_set_storage_path(t->bt, RAT_BT_KEYS_PATH);
        t->ble = bt_profile_start(t->bt, ble_profile_hid, (void*)&rat_ble_params);
        furi_check(t->ble);
        furi_hal_bt_start_advertising();
        bt_set_status_changed_callback(t->bt, rat_bt_status_cb, t);
    }

    return t;
}

void rat_transport_stop(RatTransport* t) {
    furi_check(t);
    rat_release_all(t);

    if(t->type == RatTransportUsb) {
        furi_timer_stop(t->usb_poll);
        furi_timer_free(t->usb_poll);
        furi_hal_usb_set_config(t->usb_prev, NULL);
    } else {
        bt_set_status_changed_callback(t->bt, NULL, NULL);
        bt_disconnect(t->bt);
        furi_delay_ms(200);
        bt_keys_storage_set_default_path(t->bt);
        furi_check(bt_profile_restore_default(t->bt));
        furi_record_close(RECORD_BT);
    }

    furi_mutex_free(t->mutex);
    free(t);
}

RatTransportType rat_transport_type(const RatTransport* t) {
    return t->type;
}

bool rat_transport_is_connected(const RatTransport* t) {
    return t->connected;
}

uint32_t rat_transport_report_rate(const RatTransport* t) {
    return t->type == RatTransportUsb ? 200 : 30;
}

#define LOCKED(t, usb_call, ble_call)                                                \
    do {                                                                             \
        furi_check(furi_mutex_acquire((t)->mutex, FuriWaitForever) == FuriStatusOk); \
        if((t)->type == RatTransportUsb) {                                           \
            usb_call;                                                                \
        } else {                                                                     \
            ble_call;                                                                \
        }                                                                            \
        furi_mutex_release((t)->mutex);                                              \
    } while(0)

void rat_kb_press(RatTransport* t, uint16_t chord) {
    if(!chord) return;
    LOCKED(t, furi_hal_hid_kb_press(chord), ble_profile_hid_kb_press(t->ble, chord));
}

void rat_kb_release(RatTransport* t, uint16_t chord) {
    if(!chord) return;
    LOCKED(t, furi_hal_hid_kb_release(chord), ble_profile_hid_kb_release(t->ble, chord));
}

void rat_kb_tap(RatTransport* t, uint16_t chord) {
    if(!chord) return;
    rat_kb_press(t, chord);
    furi_delay_ms(RAT_TAP_MS);
    rat_kb_release(t, chord);
}

void rat_mouse_move(RatTransport* t, int8_t dx, int8_t dy) {
    LOCKED(t, furi_hal_hid_mouse_move(dx, dy), ble_profile_hid_mouse_move(t->ble, dx, dy));
}

void rat_mouse_press(RatTransport* t, uint8_t buttons) {
    LOCKED(t, furi_hal_hid_mouse_press(buttons), ble_profile_hid_mouse_press(t->ble, buttons));
}

void rat_mouse_release(RatTransport* t, uint8_t buttons) {
    LOCKED(t, furi_hal_hid_mouse_release(buttons), ble_profile_hid_mouse_release(t->ble, buttons));
}

void rat_mouse_scroll(RatTransport* t, int8_t delta) {
    LOCKED(t, furi_hal_hid_mouse_scroll(delta), ble_profile_hid_mouse_scroll(t->ble, delta));
}

void rat_release_all(RatTransport* t) {
    furi_check(furi_mutex_acquire(t->mutex, FuriWaitForever) == FuriStatusOk);
    if(t->type == RatTransportUsb) {
        furi_hal_hid_kb_release_all();
        furi_hal_hid_consumer_key_release_all();
        furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT | HID_MOUSE_BTN_RIGHT | HID_MOUSE_BTN_WHEEL);
    } else {
        ble_profile_hid_kb_release_all(t->ble);
        ble_profile_hid_consumer_key_release_all(t->ble);
        ble_profile_hid_mouse_release_all(t->ble);
    }
    furi_mutex_release(t->mutex);
}

void rat_transport_forget_ble_pairings(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    bt_disconnect(bt);
    furi_delay_ms(200);
    furi_hal_bt_stop_advertising();
    bt_keys_storage_set_storage_path(bt, RAT_BT_KEYS_PATH);
    bt_forget_bonded_devices(bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(bt);
    furi_check(bt_profile_restore_default(bt));
    furi_record_close(RECORD_BT);
}
