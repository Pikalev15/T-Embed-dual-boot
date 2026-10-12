#!/usr/bin/env python3
"""Minimal Bruce 1.16.1 adaptation for Flipper 2.0 dual boot.

Bruce immediately arms Flipper's ota_0 for the next reset without rebooting.
Its explicit 'Reboot to Flipper' menu action writes a one-shot shared NVS key
that Flipper consumes to bypass the startup menu exactly once.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "multi-boot" / "bruce"
MAIN = ROOT / "src" / "main.cpp"
MENU = ROOT / "src" / "core" / "menu_items" / "FlipperOsMenu.cpp"

def inject(source, anchor, replacement, label):
    if replacement in source:
        return source
    if source.count(anchor) != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, got {source.count(anchor)}")
    return source.replace(anchor, replacement, 1)

def main():
    code = MAIN.read_text(encoding="utf-8")
    code = inject(
        code,
        '#include "core/main_menu.h"\n',
        '#include "core/main_menu.h"\n#include <esp_ota_ops.h>\n#include <esp_partition.h>\n',
        "OTA includes",
    )
    hook = """
// Leave Bruce running, but route its next ordinary reset through Flipper.
static void dualBootArmFlipperForNextReset() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) return;
    const esp_partition_t *flipper = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (!flipper) return;
    esp_app_desc_t description;
    if (esp_ota_get_partition_description(flipper, &description) != ESP_OK) return;
    (void)esp_ota_set_boot_partition(flipper);
}

"""
    code = inject(code, "void setup() {\n", hook + "void setup() {\n", "setup hook")
    code = inject(
        code,
        '    setBrightness(bruceConfig.bright, false);\n    // end of post gpio begin\n',
        '    setBrightness(bruceConfig.bright, false);\n    // end of post gpio begin\n'
        '    dualBootArmFlipperForNextReset();\n',
        "arm next reset",
    )
    MAIN.write_text(code, encoding="utf-8")
    menu = MENU.read_text(encoding="utf-8")
    menu = inject(
        menu,
        "#include <esp_partition.h>\n",
        "#include <esp_partition.h>\n#include <Preferences.h>\n",
        "NVS include",
    )
    menu = inject(
        menu,
        '    displayInfo("Rebooting to Flipper Zero...");\n',
        '    Preferences prefs;\n'
        '    if (!prefs.begin("dual_boot", false) || prefs.putUChar("skip_once", 1) != 1) {\n'
        '        displayError("Could not save boot preference.", true);\n'
        '        return;\n'
        '    }\n'
        '    prefs.end();\n'
        '    displayInfo("Rebooting to Flipper Zero...");\n',
        "one-shot Flipper return",
    )
    MENU.write_text(menu, encoding="utf-8")
    print("Bruce: installed only Flipper return and next-reset selector support")

if __name__ == "__main__":
    main()
