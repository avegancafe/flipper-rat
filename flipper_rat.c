/* flipper-rat: air mouse + presentation remote for Flipper Zero with the Video Game Module. */

#include "flipper_rat_i.h"

#define TAG "FlipperRat"

typedef enum {
    MenuAirMouse,
    MenuProfile,
    MenuSettings,
    MenuForget,
} MenuIndex;

/* ---------- transport / status ---------- */

static void status_cb(bool connected, void* context) {
    UNUSED(connected);
    RatApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RatEventStatusChanged);
}

static void transport_start(RatApp* app) {
    furi_check(!app->transport);
    app->transport = rat_transport_start(app->settings.transport, status_cb, app);
}

static void transport_stop(RatApp* app) {
    if(!app->transport) return;
    rat_transport_stop(app->transport);
    app->transport = NULL;
}

const char* rat_app_status_label(RatApp* app) {
    if(!app->transport) return "off";
    bool usb = rat_transport_type(app->transport) == RatTransportUsb;
    bool up = rat_transport_is_connected(app->transport);
    if(usb) return up ? "USB: connected" : "USB: waiting";
    return up ? "BT: connected" : "BT: pair me";
}

RatMotion* rat_app_motion_start(RatApp* app, RatMotionReadyCallback ready_cb, void* context) {
    if(!app->imu || !app->transport) return NULL;
    RatMotionConfig cfg = rat_settings_motion_config(&app->settings);
    return rat_motion_start(app->imu, app->transport, &cfg, app->settings.grip, ready_cb, context);
}

/* ---------- main menu ---------- */

static void menu_refresh(RatApp* app) {
    submenu_set_header(app->menu, rat_app_status_label(app));
    furi_string_printf(app->menu_label, "Profile: %s", app->profile.name);
    submenu_change_item_label(app->menu, MenuProfile, furi_string_get_cstr(app->menu_label));
}

static void profiles_menu_build(RatApp* app);

static void show_dialog(RatApp* app, RatDialogKind kind);

static void menu_cb(void* context, uint32_t index) {
    RatApp* app = context;
    switch(index) {
    case MenuAirMouse:
        view_dispatcher_switch_to_view(app->view_dispatcher, RatViewMouse);
        break;
    case MenuProfile:
        profiles_menu_build(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, RatViewProfiles);
        break;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, RatViewSettings);
        break;
    case MenuForget:
        show_dialog(app, RatDialogForgetPairings);
        break;
    default:
        break;
    }
}

/* ---------- profiles ---------- */

static void profile_path(const char* file, FuriString* out) {
    furi_string_printf(out, "%s/%s", RAT_PROFILES_DIR, file);
}

static bool load_current_profile(RatApp* app) {
    FuriString* path = furi_string_alloc();
    profile_path(app->settings.profile_file, path);
    bool ok = rat_profile_load(
        app->storage, furi_string_get_cstr(path), &app->profile, app->profile_error);
    furi_string_free(path);
    if(!ok) rat_profile_set_generic(&app->profile);
    return ok;
}

static void profiles_clear(RatApp* app) {
    for(size_t i = 0; i < app->profile_count; i++)
        free(app->profile_files[i]);
    app->profile_count = 0;
}

static void profiles_menu_cb(void* context, uint32_t index) {
    RatApp* app = context;
    if(index >= app->profile_count) return;

    RatProfile p;
    FuriString* path = furi_string_alloc();
    profile_path(app->profile_files[index], path);
    bool ok = rat_profile_load(app->storage, furi_string_get_cstr(path), &p, app->profile_error);
    furi_string_free(path);

    if(!ok) {
        show_dialog(app, RatDialogProfileError);
        return;
    }
    app->profile = p;
    strlcpy(
        app->settings.profile_file, app->profile_files[index], sizeof(app->settings.profile_file));
    rat_settings_save(app->storage, &app->settings);
    menu_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RatViewMenu);
}

static void profiles_menu_build(RatApp* app) {
    profiles_clear(app);
    submenu_reset(app->profiles_menu);
    submenu_set_header(app->profiles_menu, "Slides profile");

    File* dir = storage_file_alloc(app->storage);
    FuriString* path = furi_string_alloc();
    FuriString* label = furi_string_alloc();
    FuriString* err = furi_string_alloc();
    char name[64];
    uint32_t selected = 0;

    if(storage_dir_open(dir, RAT_PROFILES_DIR)) {
        FileInfo info;
        while(app->profile_count < RAT_MAX_PROFILES &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            size_t len = strlen(name);
            if(file_info_is_dir(&info) || len < 5 || strcmp(name + len - 4, ".txt") != 0) continue;

            RatProfile p;
            profile_path(name, path);
            if(rat_profile_load(app->storage, furi_string_get_cstr(path), &p, err)) {
                furi_string_printf(label, "%s (%s)", p.name, p.os);
            } else {
                furi_string_printf(label, "! %s", name);
            }
            size_t idx = app->profile_count++;
            app->profile_files[idx] = strdup(name);
            if(strcmp(name, app->settings.profile_file) == 0) selected = idx;
            submenu_add_item(
                app->profiles_menu, furi_string_get_cstr(label), idx, profiles_menu_cb, app);
        }
    }
    storage_dir_close(dir);
    if(app->profile_count == 0) {
        submenu_add_item(app->profiles_menu, "(no profiles found)", RAT_MAX_PROFILES, NULL, app);
    }
    submenu_set_selected_item(app->profiles_menu, selected);

    furi_string_free(err);
    furi_string_free(label);
    furi_string_free(path);
    storage_file_free(dir);
}

/* ---------- dialog ---------- */

static void dialog_cb(DialogExResult result, void* context) {
    RatApp* app = context;
    if(app->dialog_kind == RatDialogForgetPairings && result == DialogExResultRight) {
        bool ble_running = app->transport && rat_transport_type(app->transport) == RatTransportBle;
        if(ble_running) transport_stop(app);
        rat_transport_forget_ble_pairings();
        if(ble_running) transport_start(app);
        menu_refresh(app);
    }
    RatViewId back = app->dialog_kind == RatDialogProfileError ? RatViewProfiles : RatViewMenu;
    app->dialog_kind = RatDialogNone;
    view_dispatcher_switch_to_view(app->view_dispatcher, back);
}

static void show_dialog(RatApp* app, RatDialogKind kind) {
    DialogEx* d = app->dialog;
    dialog_ex_reset(d);
    dialog_ex_set_context(d, app);
    dialog_ex_set_result_callback(d, dialog_cb);
    app->dialog_kind = kind;
    if(kind == RatDialogForgetPairings) {
        dialog_ex_set_header(d, "Forget BT pairings?", 64, 2, AlignCenter, AlignTop);
        dialog_ex_set_text(
            d,
            "Hosts paired with flipper-rat\nwill need to pair again.",
            64,
            20,
            AlignCenter,
            AlignTop);
        dialog_ex_set_left_button_text(d, "Cancel");
        dialog_ex_set_right_button_text(d, "Forget");
    } else {
        dialog_ex_set_header(d, "Profile error", 64, 2, AlignCenter, AlignTop);
        dialog_ex_set_text(
            d, furi_string_get_cstr(app->profile_error), 64, 20, AlignCenter, AlignTop);
        dialog_ex_set_left_button_text(d, "Back");
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RatViewDialog);
}

/* ---------- settings ---------- */

static const char* const transport_names[] = {"Bluetooth", "USB"};
static const char* const sensitivity_names[] = {"Low", "Medium", "High"};
static const char* const off_on[] = {"Off", "On"};
static const char* const motion_names[] = {"Clutch", "Always on"};
static const char* const grip_names[] = {"Flat", "Upright"};
static const char* const horiz_names[] = {"Normal", "Inverted"};
static const char* const vert_names[] = {"Point", "Flight"};

typedef enum {
    SetTransport,
    SetSensitivity,
    SetAccel,
    SetMotion,
    SetGrip,
    SetInvertX,
    SetInvertY,
} SettingIndex;

static void setting_changed(VariableItem* item) {
    RatApp* app = variable_item_get_context(item);
    uint8_t v = variable_item_get_current_value_index(item);
    RatSettings* s = &app->settings;

    switch(variable_item_list_get_selected_item_index(app->settings_list)) {
    case SetTransport:
        /* Restarting USB/BT here would run inside the list's locked model and block the GUI
         * thread while the USB/BT services work, so do it from the dispatcher instead. */
        s->transport = v;
        variable_item_set_current_value_text(item, transport_names[v]);
        view_dispatcher_send_custom_event(app->view_dispatcher, RatEventTransportChanged);
        break;
    case SetSensitivity:
        s->sensitivity = v;
        variable_item_set_current_value_text(item, sensitivity_names[v]);
        break;
    case SetAccel:
        s->accel_curve = v;
        variable_item_set_current_value_text(item, off_on[v]);
        break;
    case SetMotion:
        s->always_on = v;
        variable_item_set_current_value_text(item, motion_names[v]);
        break;
    case SetGrip:
        s->grip = v;
        variable_item_set_current_value_text(item, grip_names[v]);
        break;
    case SetInvertX:
        s->invert_x = v;
        variable_item_set_current_value_text(item, horiz_names[v]);
        break;
    case SetInvertY:
        s->invert_y = v;
        variable_item_set_current_value_text(item, vert_names[v]);
        break;
    default:
        break;
    }
    rat_settings_save(app->storage, s);
}

static void add_setting(
    RatApp* app,
    const char* label,
    const char* const* names,
    uint8_t count,
    uint8_t value) {
    VariableItem* item =
        variable_item_list_add(app->settings_list, label, count, setting_changed, app);
    variable_item_set_current_value_index(item, value);
    variable_item_set_current_value_text(item, names[value]);
}

static void settings_build(RatApp* app) {
    const RatSettings* s = &app->settings;
    add_setting(app, "Connection", transport_names, 2, s->transport);
    add_setting(app, "Sensitivity", sensitivity_names, RatSensitivityCount, s->sensitivity);
    add_setting(app, "Accel curve", off_on, 2, s->accel_curve);
    add_setting(app, "Motion", motion_names, 2, s->always_on);
    add_setting(app, "Grip", grip_names, 2, s->grip);
    add_setting(app, "Left/right", horiz_names, 2, s->invert_x);
    add_setting(app, "Up/down", vert_names, 2, s->invert_y);
}

/* ---------- dispatcher ---------- */

static bool custom_event_cb(void* context, uint32_t event) {
    RatApp* app = context;
    switch(event) {
    case RatEventStatusChanged:
        menu_refresh(app);
        rat_mode_view_refresh(app->mouse_view);
        return true;
    case RatEventMotionReady:
        rat_mode_view_motion_ready(app->mouse_view);
        return true;
    case RatEventExitMode:
        view_dispatcher_switch_to_view(app->view_dispatcher, RatViewMenu);
        return true;
    case RatEventTransportChanged:
        if(!app->transport || rat_transport_type(app->transport) != app->settings.transport) {
            transport_stop(app);
            transport_start(app);
            menu_refresh(app);
        }
        return true;
    default:
        return false;
    }
}

static uint32_t nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t nav_menu(void* context) {
    UNUSED(context);
    return RatViewMenu;
}

static RatApp* rat_app_alloc(void) {
    RatApp* app = malloc(sizeof(RatApp));
    memset(app, 0, sizeof(RatApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->profile_error = furi_string_alloc();
    app->menu_label = furi_string_alloc();

    rat_profiles_install_defaults(app->storage);
    rat_settings_load(app->storage, &app->settings);
    load_current_profile(app);

    app->imu = rat_imu_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_cb);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuAirMouse, menu_cb, app);
    submenu_add_item(app->menu, "Profile", MenuProfile, menu_cb, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_cb, app);
    submenu_add_item(app->menu, "Forget BT pairings", MenuForget, menu_cb, app);
    view_set_previous_callback(submenu_get_view(app->menu), nav_exit);
    view_dispatcher_add_view(app->view_dispatcher, RatViewMenu, submenu_get_view(app->menu));

    app->profiles_menu = submenu_alloc();
    view_set_previous_callback(submenu_get_view(app->profiles_menu), nav_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, RatViewProfiles, submenu_get_view(app->profiles_menu));

    app->settings_list = variable_item_list_alloc();
    settings_build(app);
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), nav_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, RatViewSettings, variable_item_list_get_view(app->settings_list));

    app->dialog = dialog_ex_alloc();
    view_set_previous_callback(dialog_ex_get_view(app->dialog), nav_menu);
    view_dispatcher_add_view(app->view_dispatcher, RatViewDialog, dialog_ex_get_view(app->dialog));

    app->mouse_view = rat_mouse_view_alloc(app);
    view_set_previous_callback(rat_mode_view_get_view(app->mouse_view), nav_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, RatViewMouse, rat_mode_view_get_view(app->mouse_view));


    return app;
}

static void rat_app_free(RatApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, RatViewMouse);
    rat_mode_view_free(app->mouse_view);
    view_dispatcher_remove_view(app->view_dispatcher, RatViewDialog);
    dialog_ex_free(app->dialog);
    view_dispatcher_remove_view(app->view_dispatcher, RatViewSettings);
    variable_item_list_free(app->settings_list);
    view_dispatcher_remove_view(app->view_dispatcher, RatViewProfiles);
    submenu_free(app->profiles_menu);
    view_dispatcher_remove_view(app->view_dispatcher, RatViewMenu);
    submenu_free(app->menu);
    view_dispatcher_free(app->view_dispatcher);

    rat_imu_free(app->imu);
    profiles_clear(app);
    furi_string_free(app->menu_label);
    furi_string_free(app->profile_error);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t flipper_rat_app(void* arg) {
    UNUSED(arg);
    RatApp* app = rat_app_alloc();

    transport_start(app);
    menu_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RatViewMenu);
    view_dispatcher_run(app->view_dispatcher);

    /* Mode views stop their motion worker on exit; make sure we're out of them first. */
    transport_stop(app);
    rat_settings_save(app->storage, &app->settings);
    rat_app_free(app);
    return 0;
}
