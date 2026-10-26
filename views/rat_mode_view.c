/* The live screen: an air mouse that also changes slides with the selected profile's keys. */

#include "../flipper_rat_i.h"
#include <furi_hal_usb_hid.h>
#include <gui/elements.h>

typedef struct {
    char status[24];
    bool has_imu;
    bool ready;
    bool active;
    bool always_on;
    bool paused;
    char profile[52];
    bool has_pointer_key;
} RatModeModel;

struct RatModeView {
    View* view;
    RatApp* app;
    RatMotion* motion;

    /* Button state, GUI thread only. */
    bool ready;
    bool clutch;
    bool ok_held;
    bool ok_drag; /* OK held past the long-press time: motion drags */
    uint8_t freeze_keys; /* bit per InputKey: Back/Left/Up/Down down, freeze the cursor */
    bool paused;
    uint32_t dpad_repeat;
};

/* After letting go of a button, ignore the wobble for this long. */
#define RELEASE_SETTLE_MS 150

#define DPAD_STEP_MIN 4
#define DPAD_STEP_MAX 24

static void mode_motion_ready_cb(void* context) {
    RatModeView* mv = context;
    view_dispatcher_send_custom_event(mv->app->view_dispatcher, RatEventMotionReady);
}

static void mode_update_active(RatModeView* mv) {
    bool active = false;
    if(mv->ready) {
        /* A button that's down but not yet held long enough to drag is a click:
         * freeze the cursor so pressing it doesn't nudge the pointer. */
        bool clicking = (mv->ok_held && !mv->ok_drag) || mv->freeze_keys;
        if(mv->app->settings.always_on) {
            active = !mv->paused && !clicking;
        } else {
            /* Clutch: move while Right is held, or while dragging with OK. */
            active = (mv->clutch || mv->ok_drag) && !clicking;
        }
    }
    if(mv->motion) rat_motion_set_active(mv->motion, active);
    with_view_model(
        mv->view,
        RatModeModel * m,
        {
            m->active = active;
            m->paused = mv->paused;
            m->ready = mv->ready;
        },
        true);
}

void rat_mode_view_refresh(RatModeView* mv) {
    RatApp* app = mv->app;
    with_view_model(
        mv->view,
        RatModeModel * m,
        {
            strlcpy(m->status, rat_app_status_label(app), sizeof(m->status));
            m->has_imu = app->imu != NULL;
            m->always_on = app->settings.always_on;
            snprintf(
                m->profile, sizeof(m->profile), "%s (%s)", app->profile.name, app->profile.os);
            m->has_pointer_key = app->profile.chords[RatActionPointer] != 0;
        },
        true);
}

void rat_mode_view_motion_ready(RatModeView* mv) {
    if(!mv->motion || mv->ready) return;
    mv->ready = true;
    mode_update_active(mv);
}

static void mode_draw_cb(Canvas* canvas, void* model) {
    RatModeModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "flipper-rat");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, m->status);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(!m->has_imu) {
        canvas_draw_str(canvas, 0, 25, "No motion module:");
        canvas_draw_str(canvas, 0, 35, "d-pad moves the cursor");
        canvas_draw_str(canvas, 0, 45, "OK click  Back right-click");
        canvas_draw_str(canvas, 0, 62, "Hold Back to exit");
        return;
    }

    const char* state;
    if(!m->ready) {
        state = "Hold still...";
    } else if(m->always_on) {
        state = m->paused ? "Paused (> resumes)" : "Tracking (> pauses)";
    } else {
        state = m->active ? "Moving" : "Hold > to move";
    }
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 25, state);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 35, m->profile);
    canvas_draw_str(canvas, 0, 45, "^v slides   OK click");
    canvas_draw_str(canvas, 0, 54, m->has_pointer_key ? "< laser   Back right" : "Back right-click");
    canvas_draw_str(canvas, 0, 63, "Hold Back to exit");
}

static void mode_exit_request(RatModeView* mv) {
    view_dispatcher_send_custom_event(mv->app->view_dispatcher, RatEventExitMode);
}

static void settle(RatModeView* mv) {
    if(mv->motion) rat_motion_suppress(mv->motion, RELEASE_SETTLE_MS);
}

/* Buttons that aren't for moving hold the cursor still while down. */
static void track_freeze(RatModeView* mv, InputEvent* e) {
    if(e->type == InputTypePress) {
        mv->freeze_keys |= 1 << e->key;
    } else if(e->type == InputTypeRelease) {
        mv->freeze_keys &= ~(1 << e->key);
        settle(mv);
    }
    mode_update_active(mv);
}

/* Previous/next slide using the profile's keys, held for as long as the button is. */
static void slide_key(RatModeView* mv, InputEvent* e, RatAction action) {
    uint16_t chord = mv->app->profile.chords[action];
    if(e->type == InputTypePress) {
        rat_kb_press(mv->app->transport, chord);
    } else if(e->type == InputTypeRelease) {
        rat_kb_release(mv->app->transport, chord);
    }
}

/* Without the Video Game Module the d-pad moves the cursor. */
static void dpad_move(RatModeView* mv, InputEvent* e) {
    if(e->type == InputTypePress) mv->dpad_repeat = 0;
    if(e->type != InputTypePress && e->type != InputTypeRepeat) return;
    uint32_t step = DPAD_STEP_MIN + mv->dpad_repeat * 2;
    if(step > DPAD_STEP_MAX) step = DPAD_STEP_MAX;
    mv->dpad_repeat++;
    int8_t s = (int8_t)step;
    RatTransport* t = mv->app->transport;
    switch(e->key) {
    case InputKeyUp:
        rat_mouse_move(t, 0, -s);
        break;
    case InputKeyDown:
        rat_mouse_move(t, 0, s);
        break;
    case InputKeyLeft:
        rat_mouse_move(t, -s, 0);
        break;
    case InputKeyRight:
        rat_mouse_move(t, s, 0);
        break;
    default:
        break;
    }
}

static bool mode_input_cb(InputEvent* e, void* context) {
    RatModeView* mv = context;
    RatTransport* t = mv->app->transport;
    if(!t) return false;
    bool has_imu = mv->app->imu != NULL;

    switch(e->key) {
    case InputKeyBack:
        track_freeze(mv, e);
        if(e->type == InputTypeShort) {
            rat_mouse_press(t, HID_MOUSE_BTN_RIGHT);
            furi_delay_ms(20);
            rat_mouse_release(t, HID_MOUSE_BTN_RIGHT);
        } else if(e->type == InputTypeLong) {
            mode_exit_request(mv);
        }
        break;

    case InputKeyOk:
        if(e->type == InputTypePress) {
            rat_mouse_press(t, HID_MOUSE_BTN_LEFT);
            mv->ok_held = true;
            mv->ok_drag = false;
        } else if(e->type == InputTypeLong) {
            mv->ok_drag = true; /* held ~0.3 s: now motion drags */
        } else if(e->type == InputTypeRelease) {
            rat_mouse_release(t, HID_MOUSE_BTN_LEFT);
            mv->ok_held = mv->ok_drag = false;
            settle(mv);
        }
        mode_update_active(mv);
        break;

    case InputKeyLeft:
        if(!has_imu) {
            dpad_move(mv, e);
            break;
        }
        /* Laser pointer toggle (profile `pointer` key). */
        track_freeze(mv, e);
        if(e->type == InputTypeShort) rat_kb_tap(t, mv->app->profile.chords[RatActionPointer]);
        break;

    case InputKeyRight:
        if(!has_imu) {
            dpad_move(mv, e);
            break;
        }
        if(mv->app->settings.always_on) {
            if(e->type == InputTypeShort) mv->paused = !mv->paused;
        } else if(e->type == InputTypePress) {
            mv->clutch = true;
        } else if(e->type == InputTypeRelease) {
            mv->clutch = false;
        }
        mode_update_active(mv);
        break;

    case InputKeyUp:
        if(has_imu) {
            track_freeze(mv, e);
            slide_key(mv, e, RatActionPrev);
        } else {
            dpad_move(mv, e);
        }
        break;

    case InputKeyDown:
        if(has_imu) {
            track_freeze(mv, e);
            slide_key(mv, e, RatActionNext);
        } else {
            dpad_move(mv, e);
        }
        break;

    default:
        break;
    }
    return true;
}

static void mode_enter_cb(void* context) {
    RatModeView* mv = context;
    mv->ready = false;
    mv->clutch = mv->ok_held = mv->ok_drag = mv->paused = false;
    mv->freeze_keys = 0;
    mv->motion = rat_app_motion_start(mv->app, mode_motion_ready_cb, mv);
    rat_mode_view_refresh(mv);
    mode_update_active(mv);
}

static void mode_exit_cb(void* context) {
    RatModeView* mv = context;
    rat_motion_stop(mv->motion);
    mv->motion = NULL;
    mv->ready = false;
    if(mv->app->transport) rat_release_all(mv->app->transport);
}

RatModeView* rat_mouse_view_alloc(RatApp* app) {
    RatModeView* mv = malloc(sizeof(RatModeView));
    memset(mv, 0, sizeof(RatModeView));
    mv->app = app;
    mv->view = view_alloc();
    view_set_context(mv->view, mv);
    view_allocate_model(mv->view, ViewModelTypeLocking, sizeof(RatModeModel));
    view_set_draw_callback(mv->view, mode_draw_cb);
    view_set_input_callback(mv->view, mode_input_cb);
    view_set_enter_callback(mv->view, mode_enter_cb);
    view_set_exit_callback(mv->view, mode_exit_cb);
    return mv;
}

void rat_mode_view_free(RatModeView* mv) {
    view_free(mv->view);
    free(mv);
}

View* rat_mode_view_get_view(RatModeView* mv) {
    return mv->view;
}
