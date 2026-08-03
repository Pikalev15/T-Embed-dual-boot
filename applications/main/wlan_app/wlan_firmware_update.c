#include "wlan_firmware_update.h"

#include <furi.h>
#include <furi_hal.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_app_format.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <nvs.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FW_UPDATE_TAG "DualFwUpdate"
#define FW_UPDATE_BASE_URL \
    "https://pikalev15.github.io/T-Embed-dual-boot/firmware/t-embed-dual/latest"
#define FW_UPDATE_MANIFEST_URL FW_UPDATE_BASE_URL "/update.txt"
#define FW_UPDATE_LAYOUT "t-embed-dual-v1"
#define FW_UPDATE_MANIFEST_MAX 4096
#define FW_UPDATE_CHUNK 8192
#define FW_UPDATE_SLOT_SIZE 0x500000u

#define FW_UPDATE_NVS_NAMESPACE "dual_ota"
#define FW_UPDATE_SCHEMA 1u
#define FW_UPDATE_PHASE_IDLE 0u
#define FW_UPDATE_PHASE_PREPARING 1u
#define FW_UPDATE_PHASE_BRUCE_PENDING 2u
#define FW_UPDATE_PHASE_COMPLETE 3u
#define FW_UPDATE_PHASE_ERROR 4u

#define FW_UPDATE_KEY_SCHEMA "schema"
#define FW_UPDATE_KEY_PHASE "phase"
#define FW_UPDATE_KEY_RELEASE "release"
#define FW_UPDATE_KEY_FL_URL "fl_url"
#define FW_UPDATE_KEY_FL_SHA "fl_sha"
#define FW_UPDATE_KEY_FL_SIZE "fl_size"
#define FW_UPDATE_KEY_SSID "ssid"
#define FW_UPDATE_KEY_PASSWORD "password"
#define FW_UPDATE_KEY_ERROR "last_error"

typedef struct {
    char release[48];
    char bruce_file[64];
    uint32_t bruce_size;
    char bruce_sha256[65];
    char flipper_file[64];
    uint32_t flipper_size;
    char flipper_sha256[65];
} FirmwareUpdateManifest;

struct WlanFirmwareUpdate {
    TaskHandle_t task;
    volatile WlanFirmwareUpdatePhase phase;
    volatile uint8_t percent;
    volatile uint32_t speed_kbps;
    volatile bool cancel;
    volatile bool running;
    char ssid[33];
    char password[65];
    char status[40];
    char error[80];
};

static void fw_update_set_status(WlanFirmwareUpdate* update, const char* status) {
    strncpy(update->status, status ? status : "", sizeof(update->status) - 1);
    update->status[sizeof(update->status) - 1] = '\0';
}

static void fw_update_nvs_error(const char* message) {
    nvs_handle_t nvs;
    if(nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8(nvs, FW_UPDATE_KEY_SCHEMA, FW_UPDATE_SCHEMA);
    nvs_set_u8(nvs, FW_UPDATE_KEY_PHASE, FW_UPDATE_PHASE_ERROR);
    nvs_set_str(nvs, FW_UPDATE_KEY_ERROR, message ? message : "Unknown error");
    nvs_erase_key(nvs, FW_UPDATE_KEY_PASSWORD);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void fw_update_nvs_clear(void) {
    nvs_handle_t nvs;
    if(nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void fw_update_fail(WlanFirmwareUpdate* update, const char* message) {
    strncpy(update->error, message ? message : "Unknown error", sizeof(update->error) - 1);
    update->error[sizeof(update->error) - 1] = '\0';
    update->phase = WlanFirmwareUpdateError;
    fw_update_set_status(update, "Update failed");
    fw_update_nvs_error(update->error);
    FURI_LOG_E(FW_UPDATE_TAG, "%s", update->error);
}

static bool fw_update_is_hex_sha(const char* value) {
    if(!value || strlen(value) != 64) return false;
    for(size_t i = 0; i < 64; ++i) {
        if(!isxdigit((unsigned char)value[i])) return false;
    }
    return true;
}

static bool fw_update_safe_file(const char* value) {
    if(!value || !value[0] || strchr(value, '/') || strchr(value, '\\') || strstr(value, "..")) {
        return false;
    }
    return strlen(value) < 64;
}

static void fw_update_trim(char* value) {
    if(!value) return;
    size_t len = strlen(value);
    while(len && (value[len - 1] == '\r' || value[len - 1] == '\n' ||
                  value[len - 1] == ' ' || value[len - 1] == '\t')) {
        value[--len] = '\0';
    }
    size_t start = 0;
    while(value[start] == ' ' || value[start] == '\t') start++;
    if(start) memmove(value, value + start, strlen(value + start) + 1);
}

static bool fw_update_parse_u32(const char* value, uint32_t* output) {
    if(!value || !value[0]) return false;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if(!end || *end != '\0' || parsed == 0 || parsed > FW_UPDATE_SLOT_SIZE) return false;
    *output = (uint32_t)parsed;
    return true;
}

static bool fw_update_parse_manifest(char* text, FirmwareUpdateManifest* manifest) {
    memset(manifest, 0, sizeof(*manifest));
    unsigned schema = 0;
    char layout[40] = {0};

    char* save = NULL;
    for(char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        fw_update_trim(line);
        if(!line[0] || line[0] == '#') continue;
        char* equals = strchr(line, '=');
        if(!equals) return false;
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        fw_update_trim(key);
        fw_update_trim(value);

        if(strcmp(key, "schema") == 0) {
            schema = (unsigned)strtoul(value, NULL, 10);
        } else if(strcmp(key, "layout") == 0) {
            strncpy(layout, value, sizeof(layout) - 1);
        } else if(strcmp(key, "release") == 0) {
            strncpy(manifest->release, value, sizeof(manifest->release) - 1);
        } else if(strcmp(key, "bruce_file") == 0) {
            strncpy(manifest->bruce_file, value, sizeof(manifest->bruce_file) - 1);
        } else if(strcmp(key, "bruce_size") == 0) {
            if(!fw_update_parse_u32(value, &manifest->bruce_size)) return false;
        } else if(strcmp(key, "bruce_sha256") == 0) {
            strncpy(manifest->bruce_sha256, value, sizeof(manifest->bruce_sha256) - 1);
        } else if(strcmp(key, "flipper_file") == 0) {
            strncpy(manifest->flipper_file, value, sizeof(manifest->flipper_file) - 1);
        } else if(strcmp(key, "flipper_size") == 0) {
            if(!fw_update_parse_u32(value, &manifest->flipper_size)) return false;
        } else if(strcmp(key, "flipper_sha256") == 0) {
            strncpy(manifest->flipper_sha256, value, sizeof(manifest->flipper_sha256) - 1);
        }
    }

    return schema == FW_UPDATE_SCHEMA && strcmp(layout, FW_UPDATE_LAYOUT) == 0 &&
           manifest->release[0] && fw_update_safe_file(manifest->bruce_file) &&
           fw_update_safe_file(manifest->flipper_file) && manifest->bruce_size > 0 &&
           manifest->flipper_size > 0 && fw_update_is_hex_sha(manifest->bruce_sha256) &&
           fw_update_is_hex_sha(manifest->flipper_sha256);
}

static void fw_update_http_config(esp_http_client_config_t* config, const char* url) {
    memset(config, 0, sizeof(*config));
    config->url = url;
    config->timeout_ms = 45000;
    config->transport_type = HTTP_TRANSPORT_OVER_SSL;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->buffer_size = FW_UPDATE_CHUNK;
    config->buffer_size_tx = 1024;
    config->keep_alive_enable = true;
}

static bool fw_update_fetch_manifest(WlanFirmwareUpdate* update, FirmwareUpdateManifest* manifest) {
    char* buffer = malloc(FW_UPDATE_MANIFEST_MAX);
    if(!buffer) {
        fw_update_fail(update, "Out of memory for manifest");
        return false;
    }

    esp_http_client_config_t config;
    fw_update_http_config(&config, FW_UPDATE_MANIFEST_URL);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(!client) {
        free(buffer);
        fw_update_fail(update, "HTTP client init failed");
        return false;
    }

    bool ok = false;
    size_t length = 0;
    if(esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client) == 200) {
            while(length + 1 < FW_UPDATE_MANIFEST_MAX && !update->cancel) {
                int read = esp_http_client_read(
                    client, buffer + length, FW_UPDATE_MANIFEST_MAX - 1 - length);
                if(read < 0) {
                    length = 0;
                    break;
                }
                if(read == 0) break;
                length += (size_t)read;
            }
            buffer[length] = '\0';
            ok = length > 0 && fw_update_parse_manifest(buffer, manifest);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);

    if(!ok && !update->cancel) fw_update_fail(update, "Invalid update manifest");
    return ok;
}

static bool fw_update_store_transaction(
    WlanFirmwareUpdate* update,
    const FirmwareUpdateManifest* manifest,
    uint8_t phase) {
    char flipper_url[256];
    int written = snprintf(
        flipper_url, sizeof(flipper_url), "%s/%s", FW_UPDATE_BASE_URL, manifest->flipper_file);
    if(written <= 0 || (size_t)written >= sizeof(flipper_url)) return false;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if(err != ESP_OK) return false;

    err = nvs_erase_all(nvs);
    if(err == ESP_OK) err = nvs_set_u8(nvs, FW_UPDATE_KEY_SCHEMA, FW_UPDATE_SCHEMA);
    if(err == ESP_OK) err = nvs_set_u8(nvs, FW_UPDATE_KEY_PHASE, phase);
    if(err == ESP_OK) err = nvs_set_str(nvs, FW_UPDATE_KEY_RELEASE, manifest->release);
    if(err == ESP_OK) err = nvs_set_str(nvs, FW_UPDATE_KEY_FL_URL, flipper_url);
    if(err == ESP_OK) err = nvs_set_str(nvs, FW_UPDATE_KEY_FL_SHA, manifest->flipper_sha256);
    if(err == ESP_OK) err = nvs_set_u32(nvs, FW_UPDATE_KEY_FL_SIZE, manifest->flipper_size);
    if(err == ESP_OK) err = nvs_set_str(nvs, FW_UPDATE_KEY_SSID, update->ssid);
    if(err == ESP_OK) err = nvs_set_str(nvs, FW_UPDATE_KEY_PASSWORD, update->password);
    if(err == ESP_OK) err = nvs_erase_key(nvs, FW_UPDATE_KEY_ERROR);
    if(err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if(err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err == ESP_OK;
}

static int fw_update_hex_value(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool fw_update_hash_matches(const uint8_t digest[32], const char expected[65]) {
    for(size_t i = 0; i < 32; ++i) {
        int hi = fw_update_hex_value(expected[i * 2]);
        int lo = fw_update_hex_value(expected[i * 2 + 1]);
        if(hi < 0 || lo < 0 || digest[i] != (uint8_t)((hi << 4) | lo)) return false;
    }
    return true;
}

static bool fw_update_download_bruce(
    WlanFirmwareUpdate* update,
    const FirmwareUpdateManifest* manifest,
    const esp_partition_t* target) {
    char url[256];
    int url_length = snprintf(url, sizeof(url), "%s/%s", FW_UPDATE_BASE_URL, manifest->bruce_file);
    if(url_length <= 0 || (size_t)url_length >= sizeof(url)) {
        fw_update_fail(update, "Bruce URL too long");
        return false;
    }

    esp_http_client_config_t config;
    fw_update_http_config(&config, url);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(!client) {
        fw_update_fail(update, "HTTP client init failed");
        return false;
    }

    esp_ota_handle_t ota = 0;
    bool ota_started = false;
    bool ok = false;
    uint8_t* chunk = NULL;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);

    do {
        if(esp_http_client_open(client, 0) != ESP_OK) {
            fw_update_fail(update, "Bruce download connect failed");
            break;
        }
        esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client) != 200) {
            fw_update_fail(update, "Bruce download HTTP error");
            break;
        }
        int64_t content_length = esp_http_client_get_content_length(client);
        if(content_length > 0 && (uint64_t)content_length != manifest->bruce_size) {
            fw_update_fail(update, "Bruce download size mismatch");
            break;
        }

        esp_err_t err = esp_ota_begin(target, manifest->bruce_size, &ota);
        if(err != ESP_OK) {
            fw_update_fail(update, "Could not erase Bruce slot");
            break;
        }
        ota_started = true;
        chunk = malloc(FW_UPDATE_CHUNK);
        if(!chunk) {
            fw_update_fail(update, "Out of memory for download");
            break;
        }

        mbedtls_sha256_starts(&sha, 0);
        update->phase = WlanFirmwareUpdateDownloadingBruce;
        fw_update_set_status(update, "Downloading Bruce");
        uint32_t received = 0;
        uint32_t speed_bytes = 0;
        uint32_t speed_tick = furi_get_tick();

        while(!update->cancel && received < manifest->bruce_size) {
            size_t want = manifest->bruce_size - received;
            if(want > FW_UPDATE_CHUNK) want = FW_UPDATE_CHUNK;
            int read = esp_http_client_read(client, (char*)chunk, want);
            if(read <= 0) break;
            if(esp_ota_write(ota, chunk, (size_t)read) != ESP_OK) {
                fw_update_fail(update, "Bruce flash write failed");
                break;
            }
            mbedtls_sha256_update(&sha, chunk, (size_t)read);
            received += (uint32_t)read;
            speed_bytes += (uint32_t)read;
            update->percent = (uint8_t)(((uint64_t)received * 92u) / manifest->bruce_size);

            uint32_t now = furi_get_tick();
            uint32_t elapsed = now - speed_tick;
            if(elapsed >= 500) {
                update->speed_kbps =
                    (uint32_t)((uint64_t)speed_bytes * 1000u / 1024u / elapsed);
                speed_bytes = 0;
                speed_tick = now;
            }
        }

        if(update->cancel) break;
        if(update->phase == WlanFirmwareUpdateError) break;
        if(received != manifest->bruce_size || !esp_http_client_is_complete_data_received(client)) {
            fw_update_fail(update, "Bruce download incomplete");
            break;
        }

        update->phase = WlanFirmwareUpdateVerifyingBruce;
        fw_update_set_status(update, "Verifying Bruce");
        update->percent = 96;
        update->speed_kbps = 0;

        uint8_t digest[32];
        mbedtls_sha256_finish(&sha, digest);
        if(!fw_update_hash_matches(digest, manifest->bruce_sha256)) {
            fw_update_fail(update, "Bruce SHA-256 mismatch");
            break;
        }

        if(esp_ota_end(ota) != ESP_OK) {
            ota_started = false;
            fw_update_fail(update, "Bruce image validation failed");
            break;
        }
        ota_started = false;

        esp_app_desc_t description;
        if(esp_ota_get_partition_description(target, &description) != ESP_OK) {
            fw_update_fail(update, "Bruce image is not bootable");
            break;
        }
        ok = true;
    } while(false);

    if(ota_started) esp_ota_abort(ota);
    if(chunk) free(chunk);
    mbedtls_sha256_free(&sha);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static void fw_update_finish(WlanFirmwareUpdate* update) {
    update->running = false;
    update->task = NULL;
    vTaskDelete(NULL);
}

static void fw_update_task(void* context) {
    WlanFirmwareUpdate* update = context;
    FirmwareUpdateManifest manifest;

    const esp_partition_t* running = esp_ota_get_running_partition();
    if(!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        fw_update_fail(update, "Updater must run from Flipper");
        fw_update_finish(update);
        return;
    }

    const esp_partition_t* target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if(!target || target->size < FW_UPDATE_SLOT_SIZE) {
        fw_update_fail(update, "Bruce ota_1 slot not found");
        fw_update_finish(update);
        return;
    }

    update->phase = WlanFirmwareUpdateChecking;
    update->percent = 0;
    fw_update_set_status(update, "Checking release");
    if(!fw_update_fetch_manifest(update, &manifest)) {
        if(update->cancel) fw_update_nvs_clear();
        fw_update_finish(update);
        return;
    }

    if(update->cancel) {
        fw_update_nvs_clear();
        update->phase = WlanFirmwareUpdateIdle;
        fw_update_finish(update);
        return;
    }

    if(!fw_update_store_transaction(update, &manifest, FW_UPDATE_PHASE_PREPARING)) {
        fw_update_fail(update, "Could not save update state");
        fw_update_finish(update);
        return;
    }

    if(!fw_update_download_bruce(update, &manifest, target)) {
        if(update->cancel) {
            fw_update_nvs_clear();
            update->phase = WlanFirmwareUpdateIdle;
        }
        fw_update_finish(update);
        return;
    }

    if(!fw_update_store_transaction(update, &manifest, FW_UPDATE_PHASE_BRUCE_PENDING)) {
        fw_update_fail(update, "Could not arm Bruce stage");
        fw_update_finish(update);
        return;
    }

    if(esp_ota_set_boot_partition(target) != ESP_OK) {
        fw_update_fail(update, "Could not select Bruce slot");
        fw_update_finish(update);
        return;
    }

    update->phase = WlanFirmwareUpdateRebootingBruce;
    update->percent = 100;
    update->speed_kbps = 0;
    fw_update_set_status(update, "Rebooting to Bruce");
    FURI_LOG_I(FW_UPDATE_TAG, "Bruce written; rebooting for stage two");
    furi_delay_ms(700);
    furi_hal_power_reset();

    fw_update_finish(update);
}

WlanFirmwareUpdate* wlan_firmware_update_alloc(void) {
    WlanFirmwareUpdate* update = malloc(sizeof(WlanFirmwareUpdate));
    if(!update) return NULL;
    memset(update, 0, sizeof(*update));
    update->phase = WlanFirmwareUpdateIdle;
    fw_update_set_status(update, "Ready");
    return update;
}

void wlan_firmware_update_free(WlanFirmwareUpdate* update) {
    if(!update) return;
    wlan_firmware_update_cancel(update);
    free(update);
}

void wlan_firmware_update_start(
    WlanFirmwareUpdate* update, const char* ssid, const char* password) {
    if(!update || update->running) return;
    memset(update->ssid, 0, sizeof(update->ssid));
    memset(update->password, 0, sizeof(update->password));
    strncpy(update->ssid, ssid ? ssid : "", sizeof(update->ssid) - 1);
    strncpy(update->password, password ? password : "", sizeof(update->password) - 1);
    update->cancel = false;
    update->percent = 0;
    update->speed_kbps = 0;
    update->error[0] = '\0';
    update->phase = WlanFirmwareUpdateChecking;
    fw_update_set_status(update, "Checking release");
    update->running = true;

    if(xTaskCreate(fw_update_task, "DualFwUpdate", 12288, update, 4, &update->task) != pdPASS) {
        update->running = false;
        update->task = NULL;
        fw_update_fail(update, "Task spawn failed");
    }
}

void wlan_firmware_update_cancel(WlanFirmwareUpdate* update) {
    if(!update || !update->running) return;
    update->cancel = true;
    for(size_t i = 0; i < 200 && update->running; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

WlanFirmwareUpdatePhase wlan_firmware_update_get_phase(const WlanFirmwareUpdate* update) {
    return update ? update->phase : WlanFirmwareUpdateError;
}

uint8_t wlan_firmware_update_get_percent(const WlanFirmwareUpdate* update) {
    return update ? update->percent : 0;
}

uint32_t wlan_firmware_update_get_speed_kbps(const WlanFirmwareUpdate* update) {
    return update ? update->speed_kbps : 0;
}

const char* wlan_firmware_update_get_status(const WlanFirmwareUpdate* update) {
    return update ? update->status : "Unavailable";
}

const char* wlan_firmware_update_get_error(const WlanFirmwareUpdate* update) {
    return update ? update->error : "Updater unavailable";
}

bool wlan_firmware_update_is_running(const WlanFirmwareUpdate* update) {
    return update && update->running;
}
