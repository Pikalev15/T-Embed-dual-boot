#include <furi.h>
#include <gui/scene_manager.h>
#include <dialogs/dialogs.h>

#include <btshim.h>

#include <esp_partition.h>
#include <esp_ota_ops.h>

#include "../desktop_i.h"
#include "../views/desktop_view_lock_menu.h"
#include "../helpers/qflipper_bridge.h"
#include "desktop_scene.h"

#include "sdkconfig.h"

/* qFlipper / USB-Storage need USB-OTG (ESP32-S3 / S2 only). */
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
#define LOCK_MENU_USB_AVAILABLE true
#else
#define LOCK_MENU_USB_AVAILABLE false
#endif

void desktop_scene_lock_menu_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

static void desktop_lock_menu_show_message(
    const char* header,
    const char* text,
    const char* center_button) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();

    dialog_message_set_header(message, header, 64, 8, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, center_button, NULL);
    dialog_message_show(dialogs, message);

    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static bool desktop_lock_menu_confirm_switch_to_bruce(void) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();

    dialog_message_set_header(message, "Switch to Bruce?", 64, 8, AlignCenter, AlignTop);
    dialog_message_set_text(
        message, "The device will restart.\nUnsaved work may be lost.", 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Cancel", NULL, "Restart");

    const DialogMessageButton result = dialog_message_show(dialogs, message);

    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);

    return result == DialogMessageButtonRight;
}

/* Return ota_1 only when it contains a valid ESP application image. Merely
 * finding the partition is not enough: a blank ota_1 would otherwise expose
 * the menu item and could leave the device trying to boot empty flash. */
static const esp_partition_t* desktop_lock_menu_find_bruce_partition(void) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if(partition == NULL) return NULL;

    esp_app_desc_t app_desc;
    if(esp_ota_get_partition_description(partition, &app_desc) != ESP_OK) return NULL;

    return partition;
}

/* "Switch to Bruce" only makes sense when a valid second OTA firmware exists. */
static bool desktop_lock_menu_bruce_available(void) {
    return desktop_lock_menu_find_bruce_partition() != NULL;
}

/* Point the OTA boot slot at the Bruce firmware (ota_1) and reboot into it.
 * Bruce has the mirror-image entry that points back at ota_0. See
 * 00_Skills/multi-boot.md. */
static void desktop_lock_menu_switch_to_bruce(void) {
    const esp_partition_t* target = desktop_lock_menu_find_bruce_partition();
    if(target == NULL) {
        FURI_LOG_E("DesktopBruce", "no valid Bruce image in ota_1");
        desktop_lock_menu_show_message(
            "Bruce unavailable", "No valid Bruce firmware\nwas found in ota_1.", "OK");
        return;
    }

    const esp_err_t err = esp_ota_set_boot_partition(target);
    if(err != ESP_OK) {
        FURI_LOG_E("DesktopBruce", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        desktop_lock_menu_show_message(
            "Switch failed", "Could not select Bruce.\nCheck the serial log.", "OK");
        return;
    }

    FURI_LOG_I("DesktopBruce", "rebooting into Bruce");
    furi_delay_ms(100);
    furi_hal_power_reset();
}

static bool desktop_lock_menu_bt_enabled(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings;
    bt_get_settings(bt, &settings);
    furi_record_close(RECORD_BT);
    return settings.enabled;
}

static void desktop_lock_menu_set_bt_enabled(bool enabled) {
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings;
    bt_get_settings(bt, &settings);
    settings.enabled = enabled;
    bt_set_settings(bt, &settings);
    furi_record_close(RECORD_BT);
}

/* Rebuild the menu from the live toggle states (used on enter and after a
 * toggle, so the Enable/Disable labels track reality). */
static void desktop_scene_lock_menu_refresh(Desktop* desktop) {
    desktop_lock_menu_set_states(
        desktop->lock_menu,
        LOCK_MENU_USB_AVAILABLE,
        qflipper_bridge_is_active(),
        desktop_lock_menu_bt_enabled(),
        desktop_lock_menu_bruce_available());
}

void desktop_scene_lock_menu_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;

    desktop_lock_menu_set_callback(desktop->lock_menu, desktop_scene_lock_menu_callback, desktop);
    desktop_scene_lock_menu_refresh(desktop);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdLockMenu);
}

bool desktop_scene_lock_menu_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopLockMenuEventQflipperToggle:
            if(qflipper_bridge_is_active()) {
                qflipper_bridge_stop();
            } else {
                qflipper_bridge_start();
            }
            /* Stay in the menu; refresh so the label flips. */
            desktop_scene_lock_menu_refresh(desktop);
            consumed = true;
            break;

        case DesktopLockMenuEventUsbStorage:
            /* The USB-Storage scene stops the qFlipper bridge itself (shared
             * composite / mutual exclusion). */
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneUsbStorage);
            consumed = true;
            break;

        case DesktopLockMenuEventBluetoothToggle:
            desktop_lock_menu_set_bt_enabled(!desktop_lock_menu_bt_enabled());
            desktop_scene_lock_menu_refresh(desktop);
            consumed = true;
            break;

        case DesktopLockMenuEventBruce:
            if(desktop_lock_menu_confirm_switch_to_bruce()) {
                desktop_lock_menu_switch_to_bruce(); /* reboots; returns only on error */
            }
            consumed = true;
            break;

        case DesktopLockMenuEventMeshClients:
            /* T-Embed ist immer Master; der Master-Mesh-Service läuft on-demand in
             * der Mesh-Clients-Scene. */
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneMeshClients);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void desktop_scene_lock_menu_on_exit(void* context) {
    UNUSED(context);
}
