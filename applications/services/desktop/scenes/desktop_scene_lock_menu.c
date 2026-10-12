#include <furi.h>
#include <gui/scene_manager.h>

#include <btshim.h>
#include <wifi.h>

#include "../desktop_i.h"
#include "../views/desktop_view_lock_menu.h"
#include "../helpers/qflipper_bridge.h"
#include "furi_hal_usb_tinyusb_composite.h"
#include "desktop_scene.h"

#include "sdkconfig.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <dialogs/dialogs.h>

/* qFlipper / USB-Storage need USB-OTG (ESP32-S3 / S2 only). */
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
#define LOCK_MENU_USB_AVAILABLE true
#else
#define LOCK_MENU_USB_AVAILABLE false
#endif

/* The second OTA slot belongs exclusively to Bruce. */
static const esp_partition_t* dual_boot_bruce_partition(void) {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    esp_app_desc_t desc;
    if(!part || esp_ota_get_partition_description(part, &desc) != ESP_OK) return NULL;
    return part;
}

static void dual_boot_switch_to_bruce(void) {
    const esp_partition_t* part = dual_boot_bruce_partition();
    if(!part) {
        FURI_LOG_E("DualBoot", "Bruce image not installed in ota_1");
        return;
    }
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, "Switch to Bruce?", 64, 8, AlignCenter, AlignTop);
    dialog_message_set_text(msg, "Reboot into Bruce", 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(msg, "Cancel", NULL, "Switch");
    const DialogMessageButton button = dialog_message_show(dialogs, msg);
    dialog_message_free(msg);
    furi_record_close(RECORD_DIALOGS);
    if(button != DialogMessageButtonRight) return;
    if(esp_ota_set_boot_partition(part) != ESP_OK) {
        FURI_LOG_E("DualBoot", "failed selecting Bruce boot partition");
        return;
    }
    furi_delay_ms(100);
    furi_hal_power_reset();
}

void desktop_scene_lock_menu_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
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

static bool desktop_lock_menu_wifi_enabled(void) {
    Wifi* wifi = furi_record_open(RECORD_WIFI);
    bool enabled = wifi_is_enabled(wifi);
    furi_record_close(RECORD_WIFI);
    return enabled;
}

/* Rebuild the menu from the live toggle states (used on enter and after a
 * toggle, so the Enable/Disable labels track reality). */
void desktop_scene_lock_menu_refresh(Desktop* desktop) {
    desktop_lock_menu_set_states(
        desktop->lock_menu,
        LOCK_MENU_USB_AVAILABLE,
        qflipper_bridge_is_active(),
        desktop_lock_menu_bt_enabled(),
        desktop_lock_menu_wifi_enabled(),
        dual_boot_bruce_partition() != NULL);
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
                /* Bridge detached — now fully tear the shared composite down so
                 * the internal USB PHY routes back to USB-Serial-JTAG. esptool
                 * can flash again immediately, no reboot / BOOT+RESET needed. */
                furi_hal_usb_composite_uninstall();
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

        case DesktopLockMenuEventBluetoothToggle: {
            bool want_on = !desktop_lock_menu_bt_enabled();
            if(want_on) {
                /* Gegenseitiger Ausschluss (geteiltes Radio/RAM): erst WiFi global
                 * aus (gibt Radio + RAM frei), dann den BLE-Stack real hochfahren.
                 * bt_start_stack ist idempotent — bei bereits laufendem Stack ein
                 * No-op; nach vorheriger WiFi-Nutzung war er abgebaut. */
                Wifi* wifi = furi_record_open(RECORD_WIFI);
                if(wifi_is_enabled(wifi)) wifi_disable(wifi);
                furi_record_close(RECORD_WIFI);
                Bt* bt = furi_record_open(RECORD_BT);
                bt_start_stack(bt);
                furi_record_close(RECORD_BT);
            }
            desktop_lock_menu_set_bt_enabled(want_on);
            desktop_scene_lock_menu_refresh(desktop);
            consumed = true;
            break;
        }

        case DesktopLockMenuEventWifiToggle: {
            /* WiFi global toggeln. Enable schaltet BLE aus + persistiert, zieht das
             * Radio hoch und reconnectet zum zuletzt genutzten Netz (siehe
             * wifi_enable). Disable baut das Radio ab; BLE bleibt bewusst aus. */
            Wifi* wifi = furi_record_open(RECORD_WIFI);
            if(wifi_is_enabled(wifi)) {
                wifi_disable(wifi);
            } else {
                wifi_enable(wifi);
            }
            furi_record_close(RECORD_WIFI);
            desktop_scene_lock_menu_refresh(desktop);
            consumed = true;
            break;
        }

        case DesktopLockMenuEventMeshClients:
            /* T-Embed ist immer Master; der Master-Mesh-Service läuft on-demand in
             * der Mesh-Clients-Scene. */
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneMeshClients);
            consumed = true;
            break;

        case DesktopLockMenuEventBruce:
            dual_boot_switch_to_bruce();
            consumed = true;
            break;

        case DesktopLockMenuEventWebFs:
            /* Web-Filesystem lives in the WiFi app; launch it into that flow. */
            loader_start_detached_with_gui_error(desktop->loader, "wlan", "webfs");
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
