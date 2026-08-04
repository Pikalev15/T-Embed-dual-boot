#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <input/input.h>

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "furi_hal_power.h"

#include <stdio.h>

#define TAG "BootSelector"
#define BOOT_SELECTOR_VIEW 0
#define BOOT_SELECTOR_TICK_MS 100
#define BOOT_SELECTOR_TIMEOUT_TICKS 40

typedef enum {
    BootSelectorChoiceFlipper = 0,
    BootSelectorChoiceBruce = 1,
} BootSelectorChoice;

typedef struct {
    uint8_t selected;
    uint8_t ticks_left;
    bool bruce_available;
} BootSelectorModel;

typedef struct {
    ViewDispatcher* dispatcher;
    View* view;
    bool launch_bruce;
} BootSelector;

static const esp_partition_t* boot_selector_find_valid_ota(esp_partition_subtype_t subtype) {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
    if(partition == NULL) return NULL;

    esp_app_desc_t app_desc;
    if(esp_ota_get_partition_description(partition, &app_desc) != ESP_OK) return NULL;

    return partition;
}

static void boot_selector_draw(Canvas* canvas, void* model_ptr) {
    BootSelectorModel* model = model_ptr;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignCenter, "DUAL BOOT");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Flipper");
    canvas_draw_str_aligned(
        canvas,
        64,
        43,
        AlignCenter,
        AlignCenter,
        model->bruce_available ? "Bruce" : "Bruce missing");

    if(model->selected == BootSelectorChoiceFlipper) {
        elements_frame(canvas, 23, 17, 82, 16);
    } else if(model->bruce_available) {
        elements_frame(canvas, 23, 34, 82, 16);
    }

    char countdown[28];
    const unsigned seconds = (model->ticks_left + 9u) / 10u;
    snprintf(countdown, sizeof(countdown), "Auto Flipper in %us", seconds);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, countdown);
}

static bool boot_selector_input(InputEvent* event, void* context) {
    BootSelector* selector = context;
    bool consumed = false;
    bool choose_now = false;
    bool choose_bruce = false;

    if((event->type == InputTypeShort) || (event->type == InputTypeRepeat)) {
        if((event->key == InputKeyUp) || (event->key == InputKeyLeft)) {
            with_view_model(
                selector->view,
                BootSelectorModel * model,
                {
                    model->selected = BootSelectorChoiceFlipper;
                    model->ticks_left = BOOT_SELECTOR_TIMEOUT_TICKS;
                },
                true);
            consumed = true;
        } else if((event->key == InputKeyDown) || (event->key == InputKeyRight)) {
            with_view_model(
                selector->view,
                BootSelectorModel * model,
                {
                    if(model->bruce_available) model->selected = BootSelectorChoiceBruce;
                    model->ticks_left = BOOT_SELECTOR_TIMEOUT_TICKS;
                },
                true);
            consumed = true;
        }
    }

    if((event->key == InputKeyOk) && (event->type == InputTypeShort)) {
        with_view_model(
            selector->view,
            BootSelectorModel * model,
            {
                choose_bruce =
                    model->bruce_available && model->selected == BootSelectorChoiceBruce;
            },
            false);
        choose_now = true;
        consumed = true;
    } else if((event->key == InputKeyBack) && (event->type == InputTypeShort)) {
        choose_now = true;
        choose_bruce = false;
        consumed = true;
    }

    if(choose_now) {
        selector->launch_bruce = choose_bruce;
        view_dispatcher_stop(selector->dispatcher);
    }

    return consumed;
}

static void boot_selector_tick(void* context) {
    BootSelector* selector = context;
    bool expired = false;

    with_view_model(
        selector->view,
        BootSelectorModel * model,
        {
            if(model->ticks_left > 0) model->ticks_left--;
            expired = model->ticks_left == 0;
        },
        true);

    if(expired) {
        selector->launch_bruce = false;
        view_dispatcher_stop(selector->dispatcher);
    }
}

static bool boot_selector_switch_to_bruce(void) {
    const esp_partition_t* target =
        boot_selector_find_valid_ota(ESP_PARTITION_SUBTYPE_APP_OTA_1);
    if(target == NULL) {
        FURI_LOG_E(TAG, "valid Bruce image not found in ota_1");
        return false;
    }

    const esp_err_t err = esp_ota_set_boot_partition(target);
    if(err != ESP_OK) {
        FURI_LOG_E(TAG, "could not select ota_1: %s", esp_err_to_name(err));
        return false;
    }

    FURI_LOG_I(TAG, "booting Bruce from ota_1");
    furi_delay_ms(100);
    furi_hal_power_reset();
    return true;
}

void boot_selector_startup(void) {
    const bool bruce_available =
        boot_selector_find_valid_ota(ESP_PARTITION_SUBTYPE_APP_OTA_1) != NULL;

    /* A selector with only one valid OS adds delay without providing recovery. */
    if(!bruce_available) {
        FURI_LOG_W(TAG, "Bruce is missing; skipping startup selector");
        return;
    }

    /* app_main invokes STARTUP hooks only after all services have been launched.
     * Give Desktop a moment to attach its own view, then place this fullscreen
     * dispatcher on top so it cannot be immediately hidden during startup. */
    furi_delay_ms(250);

    BootSelector selector = {
        .dispatcher = view_dispatcher_alloc(),
        .view = view_alloc(),
        .launch_bruce = false,
    };

    view_allocate_model(selector.view, ViewModelTypeLocking, sizeof(BootSelectorModel));
    view_set_context(selector.view, &selector);
    view_set_draw_callback(selector.view, boot_selector_draw);
    view_set_input_callback(selector.view, boot_selector_input);

    with_view_model(
        selector.view,
        BootSelectorModel * model,
        {
            model->selected = BootSelectorChoiceFlipper;
            model->ticks_left = BOOT_SELECTOR_TIMEOUT_TICKS;
            model->bruce_available = bruce_available;
        },
        false);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_set_event_callback_context(selector.dispatcher, &selector);
    view_dispatcher_set_tick_event_callback(
        selector.dispatcher, boot_selector_tick, BOOT_SELECTOR_TICK_MS);
    view_dispatcher_attach_to_gui(selector.dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(selector.dispatcher, BOOT_SELECTOR_VIEW, selector.view);
    view_dispatcher_switch_to_view(selector.dispatcher, BOOT_SELECTOR_VIEW);
    view_dispatcher_run(selector.dispatcher);

    view_dispatcher_remove_view(selector.dispatcher, BOOT_SELECTOR_VIEW);
    view_free(selector.view);
    view_dispatcher_free(selector.dispatcher);
    furi_record_close(RECORD_GUI);

    if(selector.launch_bruce) {
        boot_selector_switch_to_bruce();
    }
}
