#!/usr/bin/env python3
"""Fast, offline, fail-closed checks for T-Embed Flipper v2/Bruce 1.16.1."""
from pathlib import Path
import csv

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

def partitions():
    specs = {}
    for row in csv.reader(line for line in source("partitions_multiboot.csv").splitlines()
                          if line.strip() and not line.lstrip().startswith("#")):
        assert len(row) >= 5
        name, ptype, subtype, offset, size = map(str.strip, row[:5])
        specs[name] = (ptype, subtype, int(offset, 0), int(size, 0))
    expected = {
        "nvs": ("data", "nvs", 0x9000, 0x6000),
        "otadata": ("data", "ota", 0xf000, 0x2000),
        "ota_0": ("app", "ota_0", 0x20000, 0x500000),
        "ota_1": ("app", "ota_1", 0x520000, 0x500000),
        "spiffs": ("data", "spiffs", 0xa20000, 0x5c0000),
    }
    for name, value in expected.items():
        assert specs.get(name) == value, f"{name}: expected {value}, got {specs.get(name)}"
    prev = 0
    for _, _, offset, size in sorted(specs.values(), key=lambda x: x[2]):
        assert offset >= prev, "overlapping partitions"
        assert offset + size <= 0x1000000, "beyond 16 MiB"
        prev = offset + size

def run():
    assert '"2.0.0"' in source("components/toolbox/fw_version.h")
    cfg = source("sdkconfig.defaults.esp32s3")
    assert 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_multiboot.csv"' in cfg
    partitions()

    flasher = source("buildAndFlash_T-Embed.sh")
    for item in ("--skip-bruce", "partition_offset ota_1", "partition_offset otadata",
                 "erase_region", "write_flash --flash_size detect",
                 "patchBruce.py", "platformio", "idf.py"):
        assert item in flasher, item

    fw = source("components/fw_ota/fw_ota.c")
    for item in ("fw_ota_is_bruce_layout", "slot->address == 0x520000",
                 "slot->size == 0x500000", "if(fw_ota_is_bruce_layout())",
                 "Dual-boot: update via USB flasher (Bruce slot protected)"):
        assert item in fw, item
    assert "return !fw_ota_is_bruce_layout()" in fw

    desktop = source("applications/services/desktop/scenes/desktop_scene_main.c")
    selector = source("applications/services/desktop/scenes/desktop_scene_boot_selector.c")
    config = source("applications/services/desktop/scenes/desktop_scene_config.h")
    lock = source("applications/services/desktop/scenes/desktop_scene_lock_menu.c")
    for item in ("desktop_boot_selector_should_show()", "DesktopMainEventShowBootSelector",
                 "DesktopSceneBootSelector"):
        assert item in desktop, item
    for item in ("ESP_PARTITION_SUBTYPE_APP_OTA_1", "ESP_RST_TASK_WDT",
                 "nvs_erase_key", "nvs_commit", '"dual_boot"', '"skip_once"'):
        assert item in selector, item
    assert "view_dispatcher_alloc" not in selector
    assert "ADD_SCENE(desktop, boot_selector, BootSelector)" in config
    assert "desktop_bruce_switch_confirmed" in lock
    assert "DesktopLockMenuEventWifiToggle" in lock  # upstream 2.0 UI preserved

    patcher = source("patchBruce.py")
    bruce_patch = source("tools/bruce_multiboot.patch")
    hook = source("tools/patch_bruce_boot_selector.py")
    assert "ba519c936c87b89c9c667c15b932d7faca5360d5" in patcher
    assert "DualBootUpdater" not in bruce_patch
    assert "DualBootUpdater" not in hook
    for item in ("FlipperOsMenu", "esp_ota_set_boot_partition"):
        assert item in bruce_patch, item
    for item in ("dualBootArmFlipperForNextReset", "skip_once", "Preferences",
                 "ESP_PARTITION_SUBTYPE_APP_OTA_0"):
        assert item in hook, item
    print("Flipper 2.0 / Bruce 1.16.1 dual-boot invariants passed.")

if __name__ == "__main__":
    run()
