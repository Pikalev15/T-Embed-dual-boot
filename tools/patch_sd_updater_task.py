#!/usr/bin/env python3
"""Idempotently fix the T-Embed SD updater worker task allocation.

Sor3nt's updater creates an 8192-byte task with xTaskCreate(), which allocates
its stack from scarce internal RAM. After Wi-Fi starts, the largest contiguous
internal block can be smaller than 8192 bytes, producing "Task spawn failed"
even though the board still has several MiB of PSRAM available.

This patch creates the updater task in PSRAM when supported and retains a
smaller internal-RAM fallback. It does not touch the OTA partition layout or
any multiboot switching code.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "applications" / "main" / "wlan_app" / "wlan_sd_update.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one source match, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")

    text = replace_once(
        text,
        "#include <freertos/task.h>\n",
        "#include <freertos/task.h>\n#include <freertos/idf_additions.h>\n",
        "FreeRTOS additions include",
    )

    text = replace_once(
        text,
        "struct WlanSdUpdate {\n    TaskHandle_t task;\n",
        "struct WlanSdUpdate {\n    TaskHandle_t task;\n    bool task_uses_caps;\n",
        "task allocation state",
    )

    text = replace_once(
        text,
        "static void sd_update_finish(WlanSdUpdate* u) {\n"
        "    u->running = false;\n"
        "    u->task = NULL;\n"
        "    vTaskDelete(NULL);\n"
        "}\n",
        "static void sd_update_finish(WlanSdUpdate* u) {\n"
        "    bool task_uses_caps = u->task_uses_caps;\n"
        "    u->running = false;\n"
        "    u->task = NULL;\n"
        "    u->task_uses_caps = false;\n"
        "    if(task_uses_caps) {\n"
        "        vTaskDeleteWithCaps(NULL);\n"
        "    } else {\n"
        "        vTaskDelete(NULL);\n"
        "    }\n"
        "}\n",
        "task deletion",
    )

    text = replace_once(
        text,
        "    u->task = NULL;\n    u->phase = WlanSdUpdateIdle;\n",
        "    u->task = NULL;\n    u->task_uses_caps = false;\n    u->phase = WlanSdUpdateIdle;\n",
        "allocator initialization",
    )

    text = replace_once(
        text,
        "    u->phase = WlanSdUpdateChecking;\n"
        "    u->running = true;\n"
        "    if(xTaskCreate(sd_update_task, \"WlanSdUpd\", 8192, u, 4, &u->task) != pdPASS) {\n"
        "        u->running = false;\n"
        "        u->task = NULL;\n"
        "        sd_update_fail(u, \"Task spawn failed\");\n"
        "    }\n",
        "    u->phase = WlanSdUpdateChecking;\n"
        "    u->running = true;\n"
        "\n"
        "    BaseType_t created = pdFAIL;\n"
        "#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM && \\\n"
        "    defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) && \\\n"
        "    CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM\n"
        "    // The updater performs HTTPS and SD-card I/O but no raw flash writes,\n"
        "    // so its large stack can safely live in the T-Embed's PSRAM.\n"
        "    u->task_uses_caps = true;\n"
        "    created = xTaskCreateWithCaps(\n"
        "        sd_update_task,\n"
        "        \"WlanSdUpd\",\n"
        "        8192,\n"
        "        u,\n"
        "        4,\n"
        "        &u->task,\n"
        "        MALLOC_CAP_SPIRAM);\n"
        "#endif\n"
        "\n"
        "    if(created != pdPASS) {\n"
        "        // Fallback for targets without PSRAM/external-stack support.\n"
        "        // The worker's large download buffers are heap allocated, so a\n"
        "        // 6144-byte internal stack is sufficient and less fragmented.\n"
        "        u->task_uses_caps = false;\n"
        "        created = xTaskCreate(sd_update_task, \"WlanSdUpd\", 6144, u, 4, &u->task);\n"
        "    }\n"
        "\n"
        "    if(created != pdPASS) {\n"
        "        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);\n"
        "        size_t largest_internal =\n"
        "            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);\n"
        "        FURI_LOG_E(\n"
        "            SD_UPDATE_TAG,\n"
        "            \"task create failed: internal free=%u largest=%u\",\n"
        "            (unsigned)free_internal,\n"
        "            (unsigned)largest_internal);\n"
        "        u->running = false;\n"
        "        u->task = NULL;\n"
        "        u->task_uses_caps = false;\n"
        "        sd_update_fail(u, \"Task spawn failed\");\n"
        "    }\n",
        "task creation",
    )

    TARGET.write_text(text, encoding="utf-8")
    print(f"Patched {TARGET.relative_to(ROOT)} for PSRAM-backed updater task")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
