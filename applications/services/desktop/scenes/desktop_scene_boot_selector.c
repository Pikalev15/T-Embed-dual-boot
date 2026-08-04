#include <furi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "../desktop_i.h"
#include "../views/desktop_view_boot_selector.h"
#include "desktop_scene.h"

#define TAG "DesktopBoot"
#define BOOT_SELECTOR_HALF_SECONDS 8

static const esp_partition_t* desktop_boot_selector_find_valid_ota(
    esp_partition_subtype_t subtype) {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
    if(partition == NULL) return NULL;

    esp_app_desc_t app_desc;
    if(esp_ota_get_partition_description(partition, &app_desc) != ESP_OK) return NULL;
    return partition;
}

bool desktop_boot_selector_available(void) {
    return desktop_boot_selector_find_valid_ota(ESP_PARTITION_SUBTYPE_APP_OTA_1) != NULL;
}

static void desktop_boot_selector_callback(DesktopEvent event, void* context) {
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
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

    if(!desktop_boot_selector_available()) {
        desktop_boot_selector_continue_flipper(desktop);
        return;
    }

    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneBootSelector, BOOT_SELECTOR_HALF_SECONDS);
    desktop_boot_selector_set_callback(
        desktop->boot_selector_view, desktop_boot_selector_callback, desktop);
    desktop_boot_selector_reset(
        desktop->boot_selector_view, true, BOOT_SELECTOR_HALF_SECONDS);
    view_dispatcher_switch_to_view(
        desktop->view_dispatcher, DesktopViewIdBootSelector);
}

bool desktop_scene_boot_selector_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DesktopBootSelectorEventConfirm) {
            const DesktopBootChoice choice =
                desktop_boot_selector_get_choice(desktop->boot_selector_view);
            if(choice == DesktopBootChoiceBruce) {
                desktop_boot_selector_launch_bruce(desktop);
            } else {
                desktop_boot_selector_continue_flipper(desktop);
            }
            return true;
        }

        if(event.event == DesktopBootSelectorEventCancel) {
            desktop_boot_selector_continue_flipper(desktop);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        uint32_t ticks = scene_manager_get_scene_state(
            desktop->scene_manager, DesktopSceneBootSelector);
        if(ticks > 0) ticks--;
        scene_manager_set_scene_state(
            desktop->scene_manager, DesktopSceneBootSelector, ticks);
        desktop_boot_selector_set_countdown(
            desktop->boot_selector_view, (uint8_t)ticks);

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
    desktop_boot_selector_set_callback(desktop->boot_selector_view, NULL, NULL);
}
