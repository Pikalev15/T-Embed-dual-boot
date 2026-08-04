#include <furi.h>
#include <gui/modules/dialog_ex.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <stdio.h>

#include "../desktop_i.h"
#include "desktop_scene.h"

#define TAG "DesktopBoot"
#define BOOT_SELECTOR_HALF_SECONDS 8

static char s_countdown_text[48];

static const esp_partition_t* desktop_boot_selector_find_valid_ota(
    esp_partition_subtype_t subtype) {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
    if(partition == NULL) return NULL;

    esp_app_desc_t app_desc;
    if(esp_ota_get_partition_description(partition, &app_desc) != ESP_OK) return NULL;
    return partition;
}

bool desktop_boot_selector_should_show(void) {
    if(desktop_boot_selector_find_valid_ota(ESP_PARTITION_SUBTYPE_APP_OTA_1) == NULL) {
        return false;
    }

    /* Fail safe: if the previous boot died from a panic/watchdog, skip the
     * selector once and let normal Flipper start so a bad selector cannot trap
     * the device in a permanent boot loop. */
    switch(esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        FURI_LOG_W(TAG, "skipping selector after crash/watchdog reset");
        return false;
    default:
        return true;
    }
}

static void desktop_boot_selector_result_callback(DialogExResult result, void* context) {
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, (uint32_t)result);
}

static void desktop_boot_selector_update_text(Desktop* desktop, uint32_t half_seconds) {
    const unsigned seconds = (half_seconds + 1u) / 2u;
    snprintf(
        s_countdown_text,
        sizeof(s_countdown_text),
        "Choose firmware\nAuto Flipper in %us",
        seconds);
    dialog_ex_set_text(
        desktop->mesh_pair_dialog,
        s_countdown_text,
        64,
        31,
        AlignCenter,
        AlignCenter);
}

static void desktop_boot_selector_continue_flipper(Desktop* desktop) {
    scene_manager_previous_scene(desktop->scene_manager);
}

static void desktop_boot_selector_launch_bruce(Desktop* desktop) {
    const esp_partition_t* target =
        desktop_boot_selector_find_valid_ota(ESP_PARTITION_SUBTYPE_APP_OTA_1);
    if(target == NULL) {
        FURI_LOG_E(TAG, "valid Bruce image not found in ota_1");
        desktop_boot_selector_continue_flipper(desktop);
        return;
    }

    const esp_err_t err = esp_ota_set_boot_partition(target);
    if(err != ESP_OK) {
        FURI_LOG_E(TAG, "could not select ota_1: %s", esp_err_to_name(err));
        desktop_boot_selector_continue_flipper(desktop);
        return;
    }

    FURI_LOG_I(TAG, "rebooting into Bruce/ota_1");
    furi_delay_ms(100);
    furi_hal_power_reset();
}

void desktop_scene_boot_selector_on_enter(void* context) {
    Desktop* desktop = context;

    if(!desktop_boot_selector_should_show()) {
        desktop_boot_selector_continue_flipper(desktop);
        return;
    }

    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneBootSelector, BOOT_SELECTOR_HALF_SECONDS);

    DialogEx* dialog = desktop->mesh_pair_dialog;
    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "DUAL BOOT", 64, 8, AlignCenter, AlignTop);
    desktop_boot_selector_update_text(desktop, BOOT_SELECTOR_HALF_SECONDS);
    dialog_ex_set_left_button_text(dialog, "Flipper");
    dialog_ex_set_right_button_text(dialog, "Bruce");
    dialog_ex_set_context(dialog, desktop);
    dialog_ex_set_result_callback(dialog, desktop_boot_selector_result_callback);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdMeshPair);
}

bool desktop_scene_boot_selector_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            desktop_boot_selector_launch_bruce(desktop);
            return true;
        }
        if(event.event == DialogExResultLeft) {
            desktop_boot_selector_continue_flipper(desktop);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        uint32_t ticks = scene_manager_get_scene_state(
            desktop->scene_manager, DesktopSceneBootSelector);
        if(ticks > 0) ticks--;
        scene_manager_set_scene_state(
            desktop->scene_manager, DesktopSceneBootSelector, ticks);
        desktop_boot_selector_update_text(desktop, ticks);

        if(ticks == 0) {
            desktop_boot_selector_continue_flipper(desktop);
        }
        return true;
    } else if(event.type == SceneManagerEventTypeBack) {
        desktop_boot_selector_continue_flipper(desktop);
        return true;
    }

    return false;
}

void desktop_scene_boot_selector_on_exit(void* context) {
    Desktop* desktop = context;
    dialog_ex_reset(desktop->mesh_pair_dialog);
}
