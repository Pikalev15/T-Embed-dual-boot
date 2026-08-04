#!/usr/bin/env python3
"""Patch the generated Bruce tree for the shared dual-boot selector contract.

The startup selector lives in ota_0 (Flipper). When it launches Bruce in ota_1,
Bruce immediately arms ota_0 as the *next* boot target without rebooting. Bruce
continues running normally, but any later reset or power cycle returns through
the selector instead of bypassing it.

Bruce's Flipper menu gets two explicit choices:
  - Reboot to Flipper: set a one-shot shared-NVS flag, then boot ota_0 directly
  - Dual Boot Menu: clear that flag, then boot ota_0 and show the selector

Run only after tools/bruce_multiboot.patch has created the Bruce add-on files.
The edits are idempotent and intentionally do not change the partition table.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
BRUCE = ROOT / "multi-boot" / "bruce"
HEADER = BRUCE / "src" / "core" / "menu_items" / "DualBootUpdater.h"
SOURCE = BRUCE / "src" / "core" / "menu_items" / "DualBootUpdater.cpp"
FLIPPER_MENU = BRUCE / "src" / "core" / "menu_items" / "FlipperOsMenu.cpp"
MAIN = BRUCE / "src" / "main.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one source match, found {count}")
    return text.replace(old, new, 1)


def patch_next_reset_front_door() -> None:
    header = HEADER.read_text(encoding="utf-8")
    header = replace_once(
        header,
        "bool dualBootUpdaterResumeIfPending();\n\n#endif // __DUAL_BOOT_UPDATER_H__\n",
        "bool dualBootUpdaterResumeIfPending();\n\n"
        "/** While Bruce runs from ota_1, make ota_0 the next reset target so\n"
        " * every later cold boot returns through the Flipper startup selector. */\n"
        "void dualBootArmFlipperForNextReset();\n\n"
        "#endif // __DUAL_BOOT_UPDATER_H__\n",
        "DualBootUpdater declaration",
    )
    HEADER.write_text(header, encoding="utf-8")

    source = SOURCE.read_text(encoding="utf-8")
    function = r'''

void dualBootArmFlipperForNextReset() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) return;

    const esp_partition_t *flipper = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr
    );
    if (flipper == nullptr) return;

    esp_app_desc_t description;
    if (esp_ota_get_partition_description(flipper, &description) != ESP_OK) return;

    // Do not reboot here. Bruce keeps running; only the *next* reset enters
    // ota_0, whose startup selector can launch either firmware again.
    (void)esp_ota_set_boot_partition(flipper);
}
'''
    if "void dualBootArmFlipperForNextReset()" not in source:
        source = source.rstrip() + function + "\n"
    SOURCE.write_text(source, encoding="utf-8")

    main_cpp = MAIN.read_text(encoding="utf-8")
    main_cpp = replace_once(
        main_cpp,
        "    dualBootUpdaterResumeIfPending();\n\n"
        "    // #ifndef USE_TFT_eSPI_TOUCH\n",
        "    dualBootUpdaterResumeIfPending();\n"
        "    dualBootArmFlipperForNextReset();\n\n"
        "    // #ifndef USE_TFT_eSPI_TOUCH\n",
        "Bruce startup hook",
    )
    MAIN.write_text(main_cpp, encoding="utf-8")


def patch_flipper_menu_choices() -> None:
    menu = FLIPPER_MENU.read_text(encoding="utf-8")

    menu = replace_once(
        menu,
        "#include <esp_partition.h>\n",
        "#include <esp_partition.h>\n#include <Preferences.h>\n",
        "Preferences include",
    )

    menu = replace_once(
        menu,
        "static void rebootToFlipperOs(void) {\n",
        "static bool setSkipSelectorOnce(bool skipSelector) {\n"
        "    Preferences prefs;\n"
        "    if (!prefs.begin(\"dual_boot\", false)) return false;\n\n"
        "    bool ok = true;\n"
        "    if (skipSelector) {\n"
        "        ok = prefs.putUChar(\"skip_once\", 1) == 1;\n"
        "    } else {\n"
        "        // Clear a stale direct-boot request before deliberately opening\n"
        "        // the selector. remove() may return false when the key is absent.\n"
        "        prefs.remove(\"skip_once\");\n"
        "    }\n"
        "    prefs.end();\n"
        "    return ok;\n"
        "}\n\n"
        "static void rebootToFlipperOs(bool skipSelector) {\n",
        "Flipper reboot helper",
    )

    menu = replace_once(
        menu,
        "    displayInfo(\"Rebooting to Flipper Zero...\");\n"
        "    delay(150);\n"
        "    ESP.restart();\n",
        "    if (!setSkipSelectorOnce(skipSelector)) {\n"
        "        displayError(\"Could not save boot preference.\", true);\n"
        "        return;\n"
        "    }\n"
        "    displayInfo(skipSelector ? \"Rebooting directly to Flipper...\"\n"
        "                             : \"Opening dual boot menu...\");\n"
        "    delay(150);\n"
        "    ESP.restart();\n",
        "Flipper reboot preference",
    )

    menu = replace_once(
        menu,
        "    options = {\n"
        "        {\"Reboot to Flipper\", []() { rebootToFlipperOs(); }},\n"
        "    };\n",
        "    options = {\n"
        "        {\"Reboot to Flipper\", []() { rebootToFlipperOs(true); }},\n"
        "        {\"Dual Boot Menu\", []() { rebootToFlipperOs(false); }},\n"
        "    };\n",
        "Flipper menu choices",
    )

    FLIPPER_MENU.write_text(menu, encoding="utf-8")


def main() -> int:
    for path in (HEADER, SOURCE, FLIPPER_MENU, MAIN):
        if not path.is_file():
            raise RuntimeError(f"missing patched Bruce file: {path}")

    patch_next_reset_front_door()
    patch_flipper_menu_choices()

    print(
        "Patched Bruce with selector-on-reset plus direct-Flipper and dual-boot menu choices"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
