#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WlanFirmwareUpdateIdle = 0,
    WlanFirmwareUpdateChecking,
    WlanFirmwareUpdateDownloadingBruce,
    WlanFirmwareUpdateVerifyingBruce,
    WlanFirmwareUpdateRebootingBruce,
    WlanFirmwareUpdateError,
} WlanFirmwareUpdatePhase;

typedef struct WlanFirmwareUpdate WlanFirmwareUpdate;

WlanFirmwareUpdate* wlan_firmware_update_alloc(void);
void wlan_firmware_update_free(WlanFirmwareUpdate* update);

/** Starts the staged dual-firmware update. The currently connected Wi-Fi
 * credentials are copied into shared NVS so Bruce can finish the second stage
 * after the first reboot. */
void wlan_firmware_update_start(
    WlanFirmwareUpdate* update, const char* ssid, const char* password);

/** Requests cancellation and waits for the worker to stop. */
void wlan_firmware_update_cancel(WlanFirmwareUpdate* update);

WlanFirmwareUpdatePhase wlan_firmware_update_get_phase(const WlanFirmwareUpdate* update);
uint8_t wlan_firmware_update_get_percent(const WlanFirmwareUpdate* update);
uint32_t wlan_firmware_update_get_speed_kbps(const WlanFirmwareUpdate* update);
const char* wlan_firmware_update_get_status(const WlanFirmwareUpdate* update);
const char* wlan_firmware_update_get_error(const WlanFirmwareUpdate* update);
bool wlan_firmware_update_is_running(const WlanFirmwareUpdate* update);
