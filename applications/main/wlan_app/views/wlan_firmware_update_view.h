#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef struct {
    char status[40];
    uint32_t speed_kbps;
    uint8_t percent;
} WlanFirmwareUpdateViewModel;

View* wlan_firmware_update_view_alloc(void);
void wlan_firmware_update_view_free(View* view);
void wlan_firmware_update_view_update(
    View* view, const char* status, uint8_t percent, uint32_t speed_kbps);
