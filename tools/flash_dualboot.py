#!/usr/bin/env python3
"""Safe full dual-boot flasher for LilyGO T-Embed CC1101 (ESP32-S3, 16MB).

Requires only Python 3 and esptool: python3 -m pip install --user esptool
The bundle must contain both firmware images, bootloader and the exact
dual-boot partition table. Never accepts a single-firmware bundle.
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

# This is intentionally board-specific; do not reuse for other ESP32 targets.
FLASH_BYTES = 0x1000000
PARTITION_OFFSET = 0x8000
EXPECTED = {
    "nvs":      (0x9000,   0x6000,   0x01, 0x02),
    "otadata":  (0xF000,   0x2000,   0x01, 0x00),
    "phy_init": (0x11000,  0x1000,   0x01, 0x01),
    "ota_0":    (0x20000,  0x500000, 0x00, 0x10),
    "ota_1":    (0x520000, 0x500000, 0x00, 0x11),
    "spiffs":   (0xA20000, 0x5C0000, 0x01, 0x82),
    "coredump": (0xFE0000, 0x20000,  0x01, 0x03),
}
FILES = {
    "bootloader": "bootloader/bootloader.bin",
    "partitions": "partition_table/partition-table.bin",
    "flipper": "furi_esp32.bin",
    "bruce": "bruce.bin",
}


def fail(message):
    raise SystemExit("ERROR: " + message)


def inspect_partition_binary(path):
    data = path.read_bytes()
    if not data or len(data) > 0x1000:
        fail("invalid partition table size (expected <= 4096 bytes)")
    found = {}
    for pos in range(0, len(data) - 31, 32):
        magic, typ, sub, offset, size, label, flags = struct.unpack_from(
            "<HBBII16sI", data, pos
        )
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:  # ESP-IDF MD5 trailer
            break
        if magic != 0x50AA:
            fail("invalid ESP-IDF partition table entry at byte " + str(pos))
        name = label.partition(b"\x00")[0].decode("ascii", errors="strict")
        if name in found:
            fail("duplicate partition label: " + name)
        found[name] = (offset, size, typ, sub)
    if found != EXPECTED:
        fail("partition-table.bin does not match the T-Embed dual-boot layout "
             "(ota_0=0x20000, ota_1=0x520000). Refusing to flash.")
    for name, (offset, size, _, _) in found.items():
        if offset + size > FLASH_BYTES:
            fail("partition exceeds 16MB flash: " + name)


def check_bundle(bundle):
    paths = {key: bundle / rel for key, rel in FILES.items()}
    for key, path in paths.items():
        if not path.is_file() or path.stat().st_size == 0:
            fail("missing/empty " + key + ": " + str(path))
    inspect_partition_binary(paths["partitions"])
    if paths["bootloader"].stat().st_size > PARTITION_OFFSET:
        fail("bootloader exceeds its 0x8000-byte region")
    for key in ("flipper", "bruce"):
        path = paths[key]
        if path.stat().st_size > 0x500000:
            fail(key + " image exceeds the 5 MiB OTA slot")
        if path.open("rb").read(1) != b"\xe9":
            fail(key + " is not an ESP firmware application image")

    manifest = bundle / "manifest.json"
    if manifest.is_file():
        try:
            checksums = json.loads(manifest.read_text(encoding="utf-8"))["sha256"]
            for key, path in paths.items():
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                if digest != checksums[FILES[key]]:
                    fail(key + " checksum mismatch; bundle may be incomplete")
        except (KeyError, ValueError, TypeError) as exc:
            fail("invalid bundle manifest: " + str(exc))
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--port", help="Serial port, e.g. /dev/ttyACM0 or COM3")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--dry-run", action="store_true", help="Validate bundle without writing flash")
    parser.add_argument("--yes", action="store_true", help="Skip interactive confirmation")
    args = parser.parse_args()
    paths = check_bundle(args.bundle.resolve())

    print("Validated dual-boot bundle for T-Embed CC1101 (ESP32-S3, 16 MB):")
    for label, offset, key in (
        ("bootloader", 0, "bootloader"),
        ("partitions", PARTITION_OFFSET, "partitions"),
        ("Flipper v2", 0x20000, "flipper"),
        ("Bruce", 0x520000, "bruce"),
    ):
        print("  %-12s @ 0x%06X  %s (%d bytes)" %
              (label, offset, paths[key].name, paths[key].stat().st_size))
    print("  otadata      @ 0x00F000  erased LAST to select Flipper (ota_0)")
    print("  NVS and SPIFFS will NOT be erased.")

    if args.dry_run:
        print("DRY RUN passed. No flash was written.")
        return
    if not args.port:
        fail("--port is required to flash (auto-port detection intentionally disabled)")
    if importlib.util.find_spec("esptool") is None:
        fail("esptool missing: install with python3 -m pip install --user esptool")
    if not args.yes:
        print("\nWARNING: This changes the partition table and BOTH firmware images.")
        if input("Type FLASH to continue: ").strip() != "FLASH":
            fail("cancelled")

    prefix = [
        sys.executable, "-m", "esptool", "--chip", "esp32s3",
        "--port", args.port, "--baud", str(args.baud),
    ]
    # Both images first, then erase OTA boot selection. If a write fails, the
    # last step is not reached, avoiding an intended-but-incomplete first boot.
    subprocess.run(prefix + [
        "--before", "default_reset", "--after", "no_reset",
        "write_flash", "--flash_mode", "dio", "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0", str(paths["bootloader"]),
        "0x8000", str(paths["partitions"]),
        "0x20000", str(paths["flipper"]),
        "0x520000", str(paths["bruce"]),
    ], check=True)
    subprocess.run(prefix + [
        "--before", "no_reset", "--after", "hard_reset",
        "erase_region", "0xF000", "0x2000",
    ], check=True)
    print("Dual-boot flash complete. Flipper is selected on next boot.")


if __name__ == "__main__":
    main()
