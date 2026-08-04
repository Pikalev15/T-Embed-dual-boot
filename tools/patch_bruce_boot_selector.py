#!/usr/bin/env python3
"""Patch the generated Bruce tree so the Flipper selector remains the front door.

The startup selector lives in ota_0 (Flipper). When it launches Bruce in ota_1,
Bruce immediately arms ota_0 as the *next* boot target without rebooting. Bruce
continues running normally, but any later reset or power cycle returns through
the selector instead of bypassing it.

Run only after tools/bruce_multiboot.patch has created DualBootUpdater.{h,cpp}.
The edits are idempotent and intentionally do not change the partition table.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
BRUCE = ROOT / "multi-boot" / "bruce"
HEADER = BRUCE / "src" / "core" / "menu_items" / "DualBootUpdater.h"
SOURCE = BRUCE / "src" / "core" / "menu_items" / "DualBootUpdater.cpp"
MAIN = BRUCE / "src" / "main.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one source match, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    for path in (HEADER, SOURCE, MAIN):
        if not path.is_file():
            raise RuntimeError(f"missing patched Bruce file: {path}")

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

    print("Patched Bruce to return through the Flipper startup selector on next reset")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
