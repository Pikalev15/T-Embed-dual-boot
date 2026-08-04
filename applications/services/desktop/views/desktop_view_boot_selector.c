#include <furi.h>
#include <gui/elements.h>

#include "../desktop_i.h"
#include "desktop_view_boot_selector.h"

static void desktop_boot_selector_draw(Canvas* canvas, void* model_ptr) {
    DesktopBootSelectorModel* model = model_ptr;

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);

    char title[24];
    const unsigned seconds = (model->half_seconds_left + 1u) / 2u;
    snprintf(title, sizeof(title), "DUAL BOOT  %us", seconds);
    canvas_draw_str_aligned(
        canvas, 64, 9 + STATUS_BAR_Y_SHIFT, AlignCenter, AlignCenter, title);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 64, 26 + STATUS_BAR_Y_SHIFT, AlignCenter, AlignCenter, "Flipper");
    canvas_draw_str_aligned(
        canvas,
        64,
        43 + STATUS_BAR_Y_SHIFT,
        AlignCenter,
        AlignCenter,
        model->bruce_available ? "Bruce" : "Bruce missing");

    if(model->selected == DesktopBootChoiceFlipper) {
        elements_frame(canvas, 23, 18 + STATUS_BAR_Y_SHIFT, 82, 15);
    } else if(model->bruce_available) {
        elements_frame(canvas, 23, 35 + STATUS_BAR_Y_SHIFT, 82, 15);
    }
}

static bool desktop_boot_selector_input(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    DesktopBootSelectorView* selector = context;
    bool consumed = false;
    bool redraw = false;

    if((event->type == InputTypeShort) || (event->type == InputTypeRepeat)) {
        if((event->key == InputKeyUp) || (event->key == InputKeyLeft)) {
            with_view_model(
                selector->view,
                DesktopBootSelectorModel * model,
                { model->selected = DesktopBootChoiceFlipper; },
                true);
            consumed = true;
            redraw = true;
        } else if((event->key == InputKeyDown) || (event->key == InputKeyRight)) {
            with_view_model(
                selector->view,
                DesktopBootSelectorModel * model,
                {
                    if(model->bruce_available) model->selected = DesktopBootChoiceBruce;
                },
                true);
            consumed = true;
            redraw = true;
        }
    }

    UNUSED(redraw);

    if((event->key == InputKeyOk) && (event->type == InputTypeShort)) {
        if(selector->callback) {
            selector->callback(DesktopBootSelectorEventConfirm, selector->context);
        }
        consumed = true;
    } else if((event->key == InputKeyBack) && (event->type == InputTypeShort)) {
        if(selector->callback) {
            selector->callback(DesktopBootSelectorEventCancel, selector->context);
        }
        consumed = true;
    }

    return consumed;
}

DesktopBootSelectorView* desktop_boot_selector_alloc(void) {
    DesktopBootSelectorView* selector = malloc(sizeof(DesktopBootSelectorView));
    selector->view = view_alloc();
    selector->callback = NULL;
    selector->context = NULL;

    view_allocate_model(
        selector->view, ViewModelTypeLocking, sizeof(DesktopBootSelectorModel));
    view_set_context(selector->view, selector);
    view_set_draw_callback(selector->view, desktop_boot_selector_draw);
    view_set_input_callback(selector->view, desktop_boot_selector_input);

    desktop_boot_selector_reset(selector, false, 0);
    return selector;
}

void desktop_boot_selector_free(DesktopBootSelectorView* selector) {
    furi_assert(selector);
    view_free(selector->view);
    free(selector);
}

View* desktop_boot_selector_get_view(DesktopBootSelectorView* selector) {
    furi_assert(selector);
    return selector->view;
}

void desktop_boot_selector_set_callback(
    DesktopBootSelectorView* selector,
    DesktopBootSelectorCallback callback,
    void* context) {
    furi_assert(selector);
    selector->callback = callback;
    selector->context = context;
}

void desktop_boot_selector_reset(
    DesktopBootSelectorView* selector,
    bool bruce_available,
    uint8_t half_seconds_left) {
    furi_assert(selector);
    with_view_model(
        selector->view,
        DesktopBootSelectorModel * model,
        {
            model->selected = DesktopBootChoiceFlipper;
            model->half_seconds_left = half_seconds_left;
            model->bruce_available = bruce_available;
        },
        true);
}

void desktop_boot_selector_set_countdown(
    DesktopBootSelectorView* selector,
    uint8_t half_seconds_left) {
    furi_assert(selector);
    with_view_model(
        selector->view,
        DesktopBootSelectorModel * model,
        { model->half_seconds_left = half_seconds_left; },
        true);
}

DesktopBootChoice desktop_boot_selector_get_choice(DesktopBootSelectorView* selector) {
    furi_assert(selector);
    DesktopBootChoice choice = DesktopBootChoiceFlipper;
    with_view_model(
        selector->view,
        DesktopBootSelectorModel * model,
        { choice = (DesktopBootChoice)model->selected; },
        false);
    return choice;
}
