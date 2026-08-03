#include "../wlan_app.h"
#include "../wlan_firmware_update.h"

typedef enum {
    UpdateFirmwareStateConfirm = 0,
    UpdateFirmwareStateRunning,
    UpdateFirmwareStateError,
} UpdateFirmwareState;

static void update_firmware_set_state(WlanApp* app, UpdateFirmwareState state) {
    scene_manager_set_scene_state(app->scene_manager, WlanAppSceneUpdateFirmware, state);
}

static UpdateFirmwareState update_firmware_get_state(WlanApp* app) {
    return (UpdateFirmwareState)scene_manager_get_scene_state(
        app->scene_manager, WlanAppSceneUpdateFirmware);
}

static void update_firmware_no_cb(GuiButtonType result, InputType type, void* context) {
    WlanApp* app = context;
    if(type == InputTypeShort && result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WlanAppCustomEventUpdateFirmwareCancel);
    }
}

static void update_firmware_yes_cb(GuiButtonType result, InputType type, void* context) {
    WlanApp* app = context;
    if(type == InputTypeShort && result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WlanAppCustomEventUpdateFirmwareStart);
    }
}

static void update_firmware_error_ok_cb(GuiButtonType result, InputType type, void* context) {
    WlanApp* app = context;
    if(type == InputTypeShort && result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WlanAppCustomEventUpdateFirmwareFinished);
    }
}

static void update_firmware_show_confirm(WlanApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 13, AlignCenter, AlignBottom, FontPrimary, "Update Both Firmwares");
    widget_add_text_box_element(
        app->widget,
        4,
        18,
        120,
        33,
        AlignCenter,
        AlignTop,
        "Bruce updates first. Flipper updates after two automatic restarts.",
        false);
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", update_firmware_no_cb, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Update", update_firmware_yes_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WlanAppViewWidget);
}

static void update_firmware_show_running(WlanApp* app) {
    wlan_firmware_update_view_update(
        app->view_firmware_update,
        wlan_firmware_update_get_status(app->firmware_update),
        wlan_firmware_update_get_percent(app->firmware_update),
        wlan_firmware_update_get_speed_kbps(app->firmware_update));
    view_dispatcher_switch_to_view(
        app->view_dispatcher, WlanAppViewFirmwareUpdate);
}

static void update_firmware_show_error(WlanApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 14, AlignCenter, AlignBottom, FontPrimary, "Update failed");
    widget_add_text_box_element(
        app->widget,
        2,
        20,
        124,
        31,
        AlignCenter,
        AlignTop,
        wlan_firmware_update_get_error(app->firmware_update),
        false);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "OK", update_firmware_error_ok_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WlanAppViewWidget);
}

void wlan_app_scene_update_firmware_on_enter(void* context) {
    WlanApp* app = context;
    app->update_firmware_flow = false;
    update_firmware_set_state(app, UpdateFirmwareStateConfirm);
    update_firmware_show_confirm(app);
}

bool wlan_app_scene_update_firmware_on_event(void* context, SceneManagerEvent event) {
    WlanApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WlanAppCustomEventUpdateFirmwareCancel) {
            wlan_firmware_update_cancel(app->firmware_update);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, WlanAppSceneMain);
            consumed = true;
        } else if(event.event == WlanAppCustomEventUpdateFirmwareStart) {
            update_firmware_set_state(app, UpdateFirmwareStateRunning);
            wlan_firmware_update_start(
                app->firmware_update,
                app->connected_ap.ssid,
                app->password_input);
            update_firmware_show_running(app);
            consumed = true;
        } else if(event.event == WlanAppCustomEventUpdateFirmwareFinished) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, WlanAppSceneMain);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick &&
              update_firmware_get_state(app) == UpdateFirmwareStateRunning) {
        WlanFirmwareUpdatePhase phase =
            wlan_firmware_update_get_phase(app->firmware_update);
        if(phase == WlanFirmwareUpdateError) {
            update_firmware_set_state(app, UpdateFirmwareStateError);
            update_firmware_show_error(app);
        } else {
            update_firmware_show_running(app);
        }
        consumed = true;
    }

    return consumed;
}

void wlan_app_scene_update_firmware_on_exit(void* context) {
    WlanApp* app = context;
    wlan_firmware_update_cancel(app->firmware_update);
    popup_reset(app->popup);
    widget_reset(app->widget);
    update_firmware_set_state(app, UpdateFirmwareStateConfirm);
}
