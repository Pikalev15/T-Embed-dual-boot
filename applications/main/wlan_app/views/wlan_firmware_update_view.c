#include "wlan_firmware_update_view.h"
#include "wlan_view_events.h"

#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

static void wlan_firmware_update_view_draw(Canvas* canvas, void* model) {
    WlanFirmwareUpdateViewModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 11, AlignCenter, AlignBottom, "FIRMWARE UPDATE");
    canvas_draw_line(canvas, 0, 14, 127, 14);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 64, 29, AlignCenter, AlignBottom, m->status[0] ? m->status : "Preparing");

    char percent[12];
    snprintf(percent, sizeof(percent), "%u%%", (unsigned)m->percent);
    elements_progress_bar_with_text(
        canvas, 4, 34, 120, (float)m->percent / 100.0f, percent);

    if(m->speed_kbps > 0) {
        char speed[24];
        snprintf(speed, sizeof(speed), "%lu kB/s", (unsigned long)m->speed_kbps);
        canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignBottom, speed);
    } else {
        canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignBottom, "Bruce first, then Flipper");
    }

    elements_button_left(canvas, "Cancel");
}

static bool wlan_firmware_update_view_input(InputEvent* event, void* context) {
    if(event->type == InputTypeShort && event->key == InputKeyUp) {
        view_dispatcher_send_custom_event(
            context, WlanAppCustomEventUpdateFirmwareCancel);
        return true;
    }
    return false;
}

View* wlan_firmware_update_view_alloc(void) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(WlanFirmwareUpdateViewModel));
    view_set_draw_callback(view, wlan_firmware_update_view_draw);
    view_set_input_callback(view, wlan_firmware_update_view_input);
    return view;
}

void wlan_firmware_update_view_free(View* view) {
    view_free(view);
}

void wlan_firmware_update_view_update(
    View* view, const char* status, uint8_t percent, uint32_t speed_kbps) {
    with_view_model(
        view,
        WlanFirmwareUpdateViewModel * model,
        {
            strncpy(model->status, status ? status : "", sizeof(model->status) - 1);
            model->status[sizeof(model->status) - 1] = '\0';
            model->percent = percent > 100 ? 100 : percent;
            model->speed_kbps = speed_kbps;
        },
        true);
}
