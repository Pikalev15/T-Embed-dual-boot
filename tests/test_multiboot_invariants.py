#!/usr/bin/env python3
"""Fail CI if an upstream refresh removes or reshapes the dual-boot contract."""

from __future__ import annotations

import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTITIONS = ROOT / "partitions_multiboot.csv"
BUILD_SCRIPT = ROOT / "buildAndFlash_T-Embed.sh"
PATCH_BRUCE = ROOT / "patchBruce.py"
BRUCE_PATCH = ROOT / "tools" / "bruce_multiboot.patch"
BRUCE_SELECTOR_PATCHER = ROOT / "tools" / "patch_bruce_boot_selector.py"
BOOT_SELECTOR_MANIFEST = ROOT / "applications" / "services" / "boot_selector" / "application.fam"
BOOT_SELECTOR_SOURCE = ROOT / "applications" / "services" / "boot_selector" / "boot_selector.c"
FLASH_SIZE = 0x1000000


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def load_partitions() -> dict[str, dict[str, int | str]]:
    partitions: dict[str, dict[str, int | str]] = {}
    with PARTITIONS.open(newline="", encoding="utf-8") as handle:
        rows = (line for line in handle if line.strip() and not line.lstrip().startswith("#"))
        for row in csv.reader(rows):
            if len(row) < 5:
                raise AssertionError(f"Malformed partition row: {row!r}")
            name, ptype, subtype, offset, size = (field.strip() for field in row[:5])
            partitions[name] = {
                "type": ptype,
                "subtype": subtype,
                "offset": parse_int(offset),
                "size": parse_int(size),
            }
    return partitions


def assert_partition_layout() -> None:
    partitions = load_partitions()
    expected = {
        "otadata": ("data", "ota", 0xF000, 0x2000),
        "ota_0": ("app", "ota_0", 0x20000, 0x500000),
        "ota_1": ("app", "ota_1", 0x520000, 0x500000),
        "spiffs": ("data", "spiffs", 0xA20000, 0x5C0000),
    }

    for name, (ptype, subtype, offset, size) in expected.items():
        assert name in partitions, f"Missing required partition: {name}"
        actual = partitions[name]
        assert actual == {
            "type": ptype,
            "subtype": subtype,
            "offset": offset,
            "size": size,
        }, f"Unexpected {name} definition: {actual!r}"

    ordered = sorted(partitions.items(), key=lambda item: int(item[1]["offset"]))
    previous_end = 0
    for name, entry in ordered:
        offset = int(entry["offset"])
        end = offset + int(entry["size"])
        assert offset >= previous_end, f"Partition overlap before {name}"
        assert end <= FLASH_SIZE, f"Partition {name} exceeds 16 MiB flash"
        previous_end = end


def assert_manual_flasher_contract() -> None:
    text = BUILD_SCRIPT.read_text(encoding="utf-8")
    required = (
        'PARTITIONS_CSV="${ESP32_DIR}/partitions_multiboot.csv"',
        "partition_offset ota_1",
        "partition_offset otadata",
        "erase_region",
        "write_flash --flash_size detect",
        '"${ota1_offset}" "${BRUCE_BIN}"',
    )
    for marker in required:
        assert marker in text, f"Manual flasher lost multiboot marker: {marker}"


def assert_bruce_patch_contract() -> None:
    patcher = PATCH_BRUCE.read_text(encoding="utf-8")
    for marker in (
        'BRUCE_REPO_URL = "https://github.com/BruceDevices/firmware.git"',
        'PATCH_FILE = REPO_ROOT / "tools" / "bruce_multiboot.patch"',
        'BOOT_SELECTOR_PATCHER = REPO_ROOT / "tools" / "patch_bruce_boot_selector.py"',
        "run([sys.executable, str(BOOT_SELECTOR_PATCHER)])",
        'PARTITIONS_DST_NAME = "custom_16Mb.csv"',
        "shutil.copyfile(PARTITIONS_SRC",
    ):
        assert marker in patcher, f"Bruce patcher lost marker: {marker}"

    patch = BRUCE_PATCH.read_text(encoding="utf-8")
    for marker in (
        "Flipper Zero",
        "ESP_PARTITION_SUBTYPE_APP_OTA_0",
        "esp_ota_set_boot_partition(target)",
        "Reboot to Flipper",
    ):
        assert marker in patch, f"Bruce return-to-Flipper patch lost marker: {marker}"

    selector_patcher = BRUCE_SELECTOR_PATCHER.read_text(encoding="utf-8")
    for marker in (
        "dualBootArmFlipperForNextReset",
        "ESP_PARTITION_SUBTYPE_APP_OTA_0",
        "esp_ota_set_boot_partition(flipper)",
        "dualBootUpdaterResumeIfPending();",
    ):
        assert marker in selector_patcher, f"Bruce selector return hook lost marker: {marker}"


def assert_flipper_switch_contract() -> None:
    """Confirm the compiled Flipper-side source still contains an ota_1 switch."""
    matches: list[Path] = []
    allowed_suffixes = {".c", ".cc", ".cpp", ".h", ".hpp"}
    skipped_parts = {".git", "build", "build_t_embed", ".pio", "multi-boot"}

    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in allowed_suffixes:
            continue
        if any(part in skipped_parts for part in path.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if (
            "ESP_PARTITION_SUBTYPE_APP_OTA_1" in text
            and "esp_ota_set_boot_partition" in text
        ):
            matches.append(path.relative_to(ROOT))

    assert matches, "Flipper-side source no longer contains the switch to Bruce/ota_1"


def assert_boot_selector_contract() -> None:
    manifest = BOOT_SELECTOR_MANIFEST.read_text(encoding="utf-8")
    for marker in (
        'appid="boot_selector"',
        "FlipperAppType.SERVICE",
        'entry_point="boot_selector_srv"',
        "order=210",
    ):
        assert marker in manifest, f"Boot selector manifest lost marker: {marker}"

    source = BOOT_SELECTOR_SOURCE.read_text(encoding="utf-8")
    for marker in (
        "BOOT_SELECTOR_TIMEOUT_TICKS",
        "ESP_PARTITION_SUBTYPE_APP_OTA_1",
        "esp_ota_set_boot_partition(target)",
        "Auto Flipper in %us",
        "view_dispatcher_set_tick_event_callback",
    ):
        assert marker in source, f"Boot selector source lost marker: {marker}"


def main() -> None:
    assert_partition_layout()
    assert_manual_flasher_contract()
    assert_bruce_patch_contract()
    assert_flipper_switch_contract()
    assert_boot_selector_contract()
    print("Multiboot invariants passed.")


if __name__ == "__main__":
    main()
