cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

# ---------------------------------------------------------------------------
# Patch libnet80211.a to allow raw TX of deauth/disassoc management frames.
# The closed-source WiFi blob rejects frame types 0xC0/0xA0; we patch
# ieee80211_raw_frame_sanity_check() to return 0. Idempotent — skips if
# already patched.  Runs at configure time (before library path resolution).
# ---------------------------------------------------------------------------
set(_wifi_lib "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32s3/libnet80211.a")
find_program(_xt_ar NAMES xtensa-esp32s3-elf-ar
    PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin" NO_DEFAULT_PATH)
find_program(_xt_objcopy NAMES xtensa-esp32s3-elf-objcopy
    PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin" NO_DEFAULT_PATH)

if(_xt_ar AND _xt_objcopy AND EXISTS "${_wifi_lib}")
    execute_process(
        COMMAND "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py"
            "${_wifi_lib}" "${_xt_objcopy}" "${_xt_ar}" "${_wifi_lib}"
        RESULT_VARIABLE _patch_rc OUTPUT_VARIABLE _patch_out ERROR_VARIABLE _patch_err)
    if(_patch_rc EQUAL 0)
        message(STATUS "${_patch_out}")
    else()
        message(WARNING "WiFi lib patch failed: ${_patch_err}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Upstream's WiFi/SD updater creates an 8192-byte worker stack in internal RAM.
# On the T-Embed, WiFi can fragment internal DRAM enough for xTaskCreate() to
# fail with "Task spawn failed" despite several MiB of free PSRAM. Apply an
# idempotent source patch that moves this worker stack to PSRAM and keeps a
# smaller internal-RAM fallback. This does not touch the multiboot layout.
# ---------------------------------------------------------------------------
find_program(_python3 NAMES python3)
if(NOT _python3)
    message(FATAL_ERROR "python3 is required to patch the SD updater task")
endif()
execute_process(
    COMMAND "${_python3}" "${CMAKE_SOURCE_DIR}/tools/patch_sd_updater_task.py"
    RESULT_VARIABLE _sd_patch_rc
    OUTPUT_VARIABLE _sd_patch_out
    ERROR_VARIABLE _sd_patch_err)
if(_sd_patch_rc EQUAL 0)
    message(STATUS "${_sd_patch_out}")
else()
    message(FATAL_ERROR "SD updater task patch failed: ${_sd_patch_err}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(furi_esp32)
