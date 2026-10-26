#pragma once

/* One HID keyboard+mouse transport (USB or Bluetooth), chosen at runtime.
 * All calls are serialized with a mutex, so the IMU thread and the GUI thread can both use it. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RatTransportBle = 0,
    RatTransportUsb = 1,
} RatTransportType;

typedef void (*RatTransportStatusCallback)(bool connected, void* context);

typedef struct RatTransport RatTransport;

/* Takes over USB or BT. `status_cb` may be called from another thread. */
RatTransport*
    rat_transport_start(RatTransportType type, RatTransportStatusCallback status_cb, void* context);

/* Releases everything and restores the previous USB config / default BT profile. */
void rat_transport_stop(RatTransport* t);

RatTransportType rat_transport_type(const RatTransport* t);
bool rat_transport_is_connected(const RatTransport* t);
/* Max useful mouse reports per second (BLE is much slower than USB). */
uint32_t rat_transport_report_rate(const RatTransport* t);

/* Keyboard. `chord` = KEY_MOD_* bits | HID usage; 0 is ignored. */
void rat_kb_press(RatTransport* t, uint16_t chord);
void rat_kb_release(RatTransport* t, uint16_t chord);
void rat_kb_tap(RatTransport* t, uint16_t chord);

/* Mouse. `buttons` = HID_MOUSE_BTN_* bits. */
void rat_mouse_move(RatTransport* t, int8_t dx, int8_t dy);
void rat_mouse_press(RatTransport* t, uint8_t buttons);
void rat_mouse_release(RatTransport* t, uint8_t buttons);
void rat_mouse_scroll(RatTransport* t, int8_t delta);

/* Releases every key and mouse button. */
void rat_release_all(RatTransport* t);

/* Forget hosts paired with flipper-rat's own BT key store. BLE transport must not be running. */
void rat_transport_forget_ble_pairings(void);
