#pragma once

#include <gui/view.h>
#include "desktop_events.h"

typedef enum {
    DesktopBootChoiceFlipper = 0,
    DesktopBootChoiceBruce = 1,
} DesktopBootChoice;

typedef struct DesktopBootSelectorView DesktopBootSelectorView;
typedef void (*DesktopBootSelectorCallback)(DesktopEvent event, void* context);

struct DesktopBootSelectorView {
    View* view;
    DesktopBootSelectorCallback callback;
    void* context;
};

typedef struct {
    uint8_t selected;
    uint8_t half_seconds_left;
    bool bruce_available;
} DesktopBootSelectorModel;

DesktopBootSelectorView* desktop_boot_selector_alloc(void);
void desktop_boot_selector_free(DesktopBootSelectorView* selector);
View* desktop_boot_selector_get_view(DesktopBootSelectorView* selector);

void desktop_boot_selector_set_callback(
    DesktopBootSelectorView* selector,
    DesktopBootSelectorCallback callback,
    void* context);
void desktop_boot_selector_reset(
    DesktopBootSelectorView* selector,
    bool bruce_available,
    uint8_t half_seconds_left);
void desktop_boot_selector_set_countdown(
    DesktopBootSelectorView* selector,
    uint8_t half_seconds_left);
DesktopBootChoice desktop_boot_selector_get_choice(DesktopBootSelectorView* selector);
