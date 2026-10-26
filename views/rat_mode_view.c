/* The two "live" screens: Air Mouse and Presenter.
 * Both may run the motion worker; they differ in how buttons map to HID output. */

#include "../flipper_rat_i.h"
#include <furi_hal_usb_hid.h>
#include <gui/elements.h>

typedef enum {
    RatModeMouse,
    RatModePresenter,
} RatModeKind;

typedef struct {
    RatModeKind kind;
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
    RatModeKind kind;
    RatMotion* motion;

    /* Button state, GUI thread only. */
    bool ready;
    bool clutch;
    bool ok_held;
    bool middle_held;
    bool back_held;
    bool ok_drag; /* OK held past the long-press time: motion drags */
    bool middle_drag;
    bool paused;
    bool pointer_held;
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
        if(mv->kind == RatModePresenter) {
            active = mv->pointer_held;
        } else {
            /* A button that's down but not yet held long enough to drag is a click:
             * freeze the cursor so pressing it doesn't nudge the pointer. */
            bool clicking = (mv->ok_held && !mv->ok_drag) || (mv->middle_held && !mv->middle_drag) ||
                            mv->back_held;
            bool dragging = mv->ok_drag || mv->middle_drag;
            if(mv->app->settings.always_on) {
                active = !mv->paused && !clicking;
            } else {
                /* Clutch: move while Right is held, or while dragging with a mouse button. */
                active = (mv->clutch || dragging) && !clicking;
            }
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

static void draw_header(Canvas* canvas, const RatModeModel* m, const char* title) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, m->status);
    canvas_draw_line(canvas, 0, 13, 127, 13);
}

static void draw_mouse(Canvas* canvas, const RatModeModel* m) {
    draw_header(canvas, m, "Air Mouse");
    canvas_set_font(canvas, FontSecondary);
    if(!m->has_imu) {
        canvas_draw_str(canvas, 0, 25, "No motion module:");
        canvas_draw_str(canvas, 0, 35, "d-pad moves the cursor");
        canvas_draw_str(canvas, 0, 45, "OK click  Back right-click");
    } else if(!m->ready) {
        canvas_draw_str(canvas, 0, 30, "Hold still...");
        canvas_draw_str(canvas, 0, 40, "calibrating");
    } else {
        const char* state;
        if(m->always_on) {
            state = m->paused ? "Paused (> resumes)" : "Tracking (> pauses)";
        } else {
            state = m->active ? "Moving" : "Hold > to move";
        }
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 25, state);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 36, "OK click   < middle");
        canvas_draw_str(canvas, 0, 46, "Back right   ^v scroll");
    }
    canvas_draw_str(canvas, 0, 62, "Hold Back to exit");
}

static void draw_presenter(Canvas* canvas, const RatModeModel* m) {
    draw_header(canvas, m, "Presenter");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 25, m->profile);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 36, "<> slide  ^ start  v black");
    if(m->has_imu) {
        canvas_draw_str(
            canvas, 0, 46, m->has_pointer_key ? "OK laser  hold: move" : "Hold OK: move cursor");
    } else if(m->has_pointer_key) {
        canvas_draw_str(canvas, 0, 46, "OK laser pointer");
    }
    canvas_draw_str(canvas, 0, 62, "Back stop  hold: exit");
    if(m->active) elements_frame(canvas, 0, 14, 128, 50);
}

static void mode_draw_cb(Canvas* canvas, void* model) {
    RatModeModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(m->kind == RatModeMouse) {
        draw_mouse(canvas, m);
    } else {
        draw_presenter(canvas, m);
    }
}

static void mode_exit_request(RatModeView* mv) {
    view_dispatcher_send_custom_event(mv->app->view_dispatcher, RatEventExitMode);
}

static void mouse_button(RatModeView* mv, InputEvent* e, uint8_t button, bool* held, bool* drag) {
    if(e->type == InputTypePress) {
        rat_mouse_press(mv->app->transport, button);
        *held = true;
        *drag = false;
    } else if(e->type == InputTypeLong) {
        *drag = true; /* held ~0.3 s: now motion drags */
    } else if(e->type == InputTypeRelease) {
        rat_mouse_release(mv->app->transport, button);
        *held = false;
        *drag = false;
        if(mv->motion) rat_motion_suppress(mv->motion, RELEASE_SETTLE_MS);
    }
}

static bool mouse_input(RatModeView* mv, InputEvent* e) {
    RatTransport* t = mv->app->transport;

    if(e->key == InputKeyBack) {
        if(e->type == InputTypePress) {
            mv->back_held = true;
        } else if(e->type == InputTypeRelease) {
            mv->back_held = false;
            if(mv->motion) rat_motion_suppress(mv->motion, RELEASE_SETTLE_MS);
        }
        mode_update_active(mv);
        if(e->type == InputTypeShort) {
            rat_mouse_press(t, HID_MOUSE_BTN_RIGHT);
            furi_delay_ms(20);
            rat_mouse_release(t, HID_MOUSE_BTN_RIGHT);
        } else if(e->type == InputTypeLong) {
            mode_exit_request(mv);
        }
        return true;
    }

    if(e->key == InputKeyOk) {
        mouse_button(mv, e, HID_MOUSE_BTN_LEFT, &mv->ok_held, &mv->ok_drag);
        mode_update_active(mv);
        return true;
    }

    if(!mv->app->imu) {
        /* D-pad mouse fallback. */
        if(e->type == InputTypePress) mv->dpad_repeat = 0;
        if(e->type != InputTypePress && e->type != InputTypeRepeat) return true;
        uint32_t step = DPAD_STEP_MIN + mv->dpad_repeat * 2;
        if(step > DPAD_STEP_MAX) step = DPAD_STEP_MAX;
        mv->dpad_repeat++;
        int8_t s = (int8_t)step;
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
        return true;
    }

    switch(e->key) {
    case InputKeyRight:
        if(mv->app->settings.always_on) {
            if(e->type == InputTypeShort) mv->paused = !mv->paused;
        } else if(e->type == InputTypePress) {
            mv->clutch = true;
        } else if(e->type == InputTypeRelease) {
            mv->clutch = false;
        }
        mode_update_active(mv);
        break;
    case InputKeyLeft:
        mouse_button(mv, e, HID_MOUSE_BTN_WHEEL, &mv->middle_held, &mv->middle_drag);
        mode_update_active(mv);
        break;
    case InputKeyUp:
        if(e->type == InputTypePress || e->type == InputTypeRepeat) rat_mouse_scroll(t, 1);
        break;
    case InputKeyDown:
        if(e->type == InputTypePress || e->type == InputTypeRepeat) rat_mouse_scroll(t, -1);
        break;
    default:
        break;
    }
    return true;
}

static void hold_chord(RatModeView* mv, InputEvent* e, RatAction action) {
    uint16_t chord = mv->app->profile.chords[action];
    if(e->type == InputTypePress) {
        rat_kb_press(mv->app->transport, chord);
    } else if(e->type == InputTypeRelease) {
        rat_kb_release(mv->app->transport, chord);
    }
}

static bool presenter_input(RatModeView* mv, InputEvent* e) {
    RatTransport* t = mv->app->transport;
    const RatProfile* p = &mv->app->profile;

    switch(e->key) {
    case InputKeyRight:
        hold_chord(mv, e, RatActionNext);
        break;
    case InputKeyLeft:
        hold_chord(mv, e, RatActionPrev);
        break;
    case InputKeyUp:
        hold_chord(mv, e, RatActionStart);
        break;
    case InputKeyDown:
        hold_chord(mv, e, RatActionBlack);
        break;
    case InputKeyOk:
        if(e->type == InputTypeShort) {
            rat_kb_tap(t, p->chords[RatActionPointer]);
        } else if(e->type == InputTypeLong) {
            mv->pointer_held = true;
            mode_update_active(mv);
        } else if(e->type == InputTypeRelease && mv->pointer_held) {
            mv->pointer_held = false;
            mode_update_active(mv);
        }
        break;
    case InputKeyBack:
        if(e->type == InputTypeShort) {
            rat_kb_tap(t, p->chords[RatActionStop]);
        } else if(e->type == InputTypeLong) {
            mode_exit_request(mv);
        }
        break;
    default:
        break;
    }
    return true;
}

static bool mode_input_cb(InputEvent* event, void* context) {
    RatModeView* mv = context;
    if(!mv->app->transport) return false;
    return mv->kind == RatModeMouse ? mouse_input(mv, event) : presenter_input(mv, event);
}

static void mode_enter_cb(void* context) {
    RatModeView* mv = context;
    mv->ready = false;
    mv->clutch = mv->ok_held = mv->middle_held = mv->paused = mv->pointer_held = false;
    mv->back_held = mv->ok_drag = mv->middle_drag = false;
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

static RatModeView* mode_view_alloc(RatApp* app, RatModeKind kind) {
    RatModeView* mv = malloc(sizeof(RatModeView));
    memset(mv, 0, sizeof(RatModeView));
    mv->app = app;
    mv->kind = kind;
    mv->view = view_alloc();
    view_set_context(mv->view, mv);
    view_allocate_model(mv->view, ViewModelTypeLocking, sizeof(RatModeModel));
    view_set_draw_callback(mv->view, mode_draw_cb);
    view_set_input_callback(mv->view, mode_input_cb);
    view_set_enter_callback(mv->view, mode_enter_cb);
    view_set_exit_callback(mv->view, mode_exit_cb);
    with_view_model(mv->view, RatModeModel * m, { m->kind = kind; }, false);
    return mv;
}

RatModeView* rat_mouse_view_alloc(RatApp* app) {
    return mode_view_alloc(app, RatModeMouse);
}

RatModeView* rat_presenter_view_alloc(RatApp* app) {
    return mode_view_alloc(app, RatModePresenter);
}

void rat_mode_view_free(RatModeView* mv) {
    view_free(mv->view);
    free(mv);
}

View* rat_mode_view_get_view(RatModeView* mv) {
    return mv->view;
}
