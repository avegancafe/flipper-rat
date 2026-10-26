#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/dialog_ex.h>
#include <storage/storage.h>

#include "transport/rat_transport.h"
#include "imu/rat_motion.h"
#include "profile/rat_profile.h"

#define RAT_MAX_PROFILES 32

typedef enum {
    RatViewMenu,
    RatViewProfiles,
    RatViewSettings,
    RatViewDialog,
    RatViewMouse,
} RatViewId;

typedef enum {
    RatEventStatusChanged = 100,
    RatEventMotionReady,
    RatEventExitMode,
    RatEventTransportChanged,
} RatCustomEvent;

typedef enum {
    RatDialogNone,
    RatDialogForgetPairings,
    RatDialogProfileError,
} RatDialogKind;

typedef struct RatApp RatApp;

/* The live mouse + slides screen (views/rat_mode_view.c). The app wires its View into the dispatcher. */
typedef struct RatModeView RatModeView;
RatModeView* rat_mouse_view_alloc(RatApp* app);
void rat_mode_view_free(RatModeView* mv);
View* rat_mode_view_get_view(RatModeView* mv);
/* Called on the GUI thread when connection status or motion readiness changes. */
void rat_mode_view_refresh(RatModeView* mv);
void rat_mode_view_motion_ready(RatModeView* mv);

struct RatApp {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* menu;
    Submenu* profiles_menu;
    VariableItemList* settings_list;
    DialogEx* dialog;
    RatDialogKind dialog_kind;
    RatModeView* mouse_view;

    RatSettings settings;
    RatProfile profile;
    FuriString* profile_error;
    FuriString* menu_label;

    RatTransport* transport;
    RatImu* imu; /* NULL when the Video Game Module isn't attached */

    char* profile_files[RAT_MAX_PROFILES];
    size_t profile_count;
};

/* Start a motion worker with the current settings, or NULL without a motion sensor. */
RatMotion* rat_app_motion_start(RatApp* app, RatMotionReadyCallback ready_cb, void* context);
/* Short connection label like "BT: connected" / "USB: waiting". */
const char* rat_app_status_label(RatApp* app);
