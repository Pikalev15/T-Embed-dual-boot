#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/submenu.h>
#include <assets_icons.h>
#include <applications.h>
#include <archive/helpers/archive_favorites.h>
#include <dialogs/dialogs.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include "loader.h"
#include "loader_menu.h"

#define TAG "LoaderMenu"

struct LoaderMenu {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
};

static int32_t loader_menu_thread(void* p);

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context) {
    LoaderMenu* loader_menu = malloc(sizeof(LoaderMenu));
    loader_menu->closed_cb = closed_cb;
    loader_menu->context = context;
    loader_menu->thread = furi_thread_alloc_ex(TAG, 1024, loader_menu_thread, loader_menu);
    furi_thread_start(loader_menu->thread);
    return loader_menu;
}

void loader_menu_free(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_join(loader_menu->thread);
    furi_thread_free(loader_menu->thread);
    free(loader_menu);
}

typedef enum {
    LoaderMenuViewPrimary,
    LoaderMenuViewSettings,
} LoaderMenuView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* primary_menu;
    Submenu* settings_menu;
} LoaderMenuApp;

static void loader_menu_start(const char* name) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_start_with_gui_error(loader, name, NULL);
    furi_record_close(RECORD_LOADER);
}

/* Main icon menu: only offer Bruce for the exact T-Embed dual-boot layout.
 * esp_ota_set_boot_partition() validates the target app before switching.
 * A valid image is not proof that it will run without crashing on hardware. */
static const esp_partition_t* loader_menu_bruce_partition(void) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* bruce = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t* scratch = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    esp_app_desc_t desc;
    if(!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
       running->address != 0x20000 || !bruce || bruce->address != 0x520000 ||
       bruce->size != 0x500000 || !scratch || scratch->address != 0xA20000 ||
       esp_ota_get_partition_description(bruce, &desc) != ESP_OK) {
        return NULL;
    }
    return bruce;
}

static void loader_menu_bruce_callback(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index);
    const esp_partition_t* bruce = loader_menu_bruce_partition();
    if(!bruce) return;

    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* dialog = dialog_message_alloc();
    dialog_message_set_header(dialog, "Boot Bruce OS?", 64, 8, AlignCenter, AlignTop);
    dialog_message_set_text(dialog, "Reboot into Bruce", 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(dialog, "Cancel", NULL, "Boot");
    DialogMessageButton answer = dialog_message_show(dialogs, dialog);
    if(answer == DialogMessageButtonRight) {
        esp_err_t err = esp_ota_set_boot_partition(bruce);
        if(err == ESP_OK) {
            const esp_partition_t* selected = esp_ota_get_boot_partition();
            if(!selected || selected->address != bruce->address) {
                FURI_LOG_E(TAG, "OTA selection failed: expected Bruce 0x%lx, read 0x%lx",
                           (unsigned long)bruce->address,
                           selected ? (unsigned long)selected->address : 0UL);
                dialog_message_set_header(dialog, "Boot selection error", 64, 8, AlignCenter, AlignTop);
                dialog_message_set_text(dialog, "OTA state did not stick", 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(dialog, NULL, "OK", NULL);
                dialog_message_show(dialogs, dialog);
                dialog_message_free(dialog);
                furi_record_close(RECORD_DIALOGS);
                return;
            }
            dialog_message_free(dialog);
            furi_record_close(RECORD_DIALOGS);
            FURI_LOG_I(TAG, "Switching to Bruce at 0x%lx", (unsigned long)bruce->address);
            furi_delay_ms(100);
            esp_restart();
            return;
        }
        FURI_LOG_E(TAG, "Bruce image rejected (esp_ota_set_boot_partition: %d)", err);
        dialog_message_set_header(dialog, "Bruce invalid", 64, 8, AlignCenter, AlignTop);
        dialog_message_set_text(dialog, "Firmware check failed", 64, 30, AlignCenter, AlignCenter);
        dialog_message_set_buttons(dialog, NULL, "OK", NULL);
        dialog_message_show(dialogs, dialog);
    }
    dialog_message_free(dialog);
    furi_record_close(RECORD_DIALOGS);
}

static void loader_menu_apps_callback(void* context, uint32_t index) {
    UNUSED(context);
    const char* name = FLIPPER_APPS[index].name;
    loader_menu_start(name);
}

static void loader_menu_external_apps_callback(void* context, uint32_t index) {
    UNUSED(context);
    const char* path = FLIPPER_EXTERNAL_APPS[index].name;
    loader_menu_start(path);
}

static void loader_menu_applications_callback(void* context, uint32_t index) {
    UNUSED(index);
    UNUSED(context);
    const char* name = LOADER_APPLICATIONS_NAME;
    loader_menu_start(name);
}

static void
    loader_menu_settings_menu_callback(void* context, InputType input_type, uint32_t index) {
    UNUSED(context);
    if(input_type == InputTypeShort) {
        loader_menu_start((const char*)index);
    } else if(input_type == InputTypeLong) {
        archive_favorites_handle_setting_pin_unpin((const char*)index, NULL);
    }
}

static void loader_menu_switch_to_settings(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenuApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewSettings);
}

static uint32_t loader_menu_switch_to_primary(void* context) {
    UNUSED(context);
    return LoaderMenuViewPrimary;
}

static uint32_t loader_menu_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void loader_menu_build_menu(LoaderMenuApp* app, LoaderMenu* menu) {
    size_t i = 0;

    menu_add_item(
        app->primary_menu,
        LOADER_APPLICATIONS_NAME,
        &A_Plugins_14,
        i++,
        loader_menu_applications_callback,
        (void*)menu);

    if(loader_menu_bruce_partition()) {
        menu_add_item(
            app->primary_menu,
            "Bruce OS",
            &A_Streaming_14,
            i++,
            loader_menu_bruce_callback,
            app);
    }

    for(i = 0; i < FLIPPER_APPS_COUNT; i++) {
        menu_add_item(
            app->primary_menu,
            FLIPPER_APPS[i].name,
            FLIPPER_APPS[i].icon,
            i,
            loader_menu_apps_callback,
            (void*)menu);
    }

    for(i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        menu_add_item(
            app->primary_menu,
            FLIPPER_EXTERNAL_APPS[i].name,
            FLIPPER_EXTERNAL_APPS[i].icon,
            i,
            loader_menu_external_apps_callback,
            (void*)menu);
    }

    menu_add_item(
        app->primary_menu, "Settings", &A_Settings_14, i++, loader_menu_switch_to_settings, app);
}

static void loader_menu_build_submenu(LoaderMenuApp* app, LoaderMenu* loader_menu) {
    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_EXTSETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_EXTSETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
    for(size_t i = 0; i < FLIPPER_SETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_SETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_SETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
}

static LoaderMenuApp* loader_menu_app_alloc(LoaderMenu* loader_menu) {
    LoaderMenuApp* app = malloc(sizeof(LoaderMenuApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->primary_menu = menu_alloc();
    app->settings_menu = submenu_alloc();

    loader_menu_build_menu(app, loader_menu);
    loader_menu_build_submenu(app, loader_menu);

    // Primary menu
    View* primary_view = menu_get_view(app->primary_menu);
    view_set_context(primary_view, app->primary_menu);
    view_set_previous_callback(primary_view, loader_menu_exit);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewPrimary, primary_view);

    // Settings menu
    View* settings_view = submenu_get_view(app->settings_menu);
    view_set_context(settings_view, app->settings_menu);
    view_set_previous_callback(settings_view, loader_menu_switch_to_primary);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewSettings, settings_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewPrimary);

    return app;
}

static void loader_menu_app_free(LoaderMenuApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewPrimary);
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewSettings);
    view_dispatcher_free(app->view_dispatcher);

    menu_free(app->primary_menu);
    submenu_free(app->settings_menu);
    furi_record_close(RECORD_GUI);
    free(app);
}

static int32_t loader_menu_thread(void* p) {
    LoaderMenu* loader_menu = p;
    furi_assert(loader_menu);

    LoaderMenuApp* app = loader_menu_app_alloc(loader_menu);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->view_dispatcher);

    if(loader_menu->closed_cb) {
        loader_menu->closed_cb(loader_menu->context);
    }

    loader_menu_app_free(app);

    return 0;
}
