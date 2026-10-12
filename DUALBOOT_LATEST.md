# Latest Sor3nt Flipper + Bruce 1.16.1 dual-boot rebuild

**Status:** Experimental / uncompiled / untested on device. This is not a stable release.

## Firmware sources

- Flipper: **Sor3nt/Flipper-Zero-ESP32-Port main** at `0e367fb5c7c7a0e1b3b3467eff412c06a7661b86`.
  This includes **36 commits after v2.0.0**, including new AirPrint tools, power-service updates, and board-support fixes.
- Bruce: official upstream **1.16.1** at `ba519c936c87b89c9c667c15b932d7faca5360d5`.
- Board: LilyGO T-Embed CC1101 / Plus with 16 MB flash and ESP32-S3.

This fork's `main` tracks the official Sor3nt upstream; **this branch is the experimental dual-boot fork**. It adds only two-way boot switching, a shared partition table, protective OTA guards, and packaging scripts. Earlier custom staged OTA/boot selector/NVS bypass code has **not** been re-added.

## Switching

- Flipper: Desktop lock menu -> **Switch to Bruce**, visible only when a valid Bruce OTA image exists.
- Bruce: Main menu -> **Flipper Zero** -> **Reboot to Flipper**.
- No boot-time firmware selector. Switching always happens from within the running firmware.

## Shared 16 MB partition layout

| Partition | Offset | Size | Owner |
|---|---|---|---|
| nvs | 0x9000 | 0x6000 | Both |
| otadata | 0xF000 | 0x2000 | Bootloader |
| phy_init | 0x11000 | 0x1000 | Both |
| ota_0 | 0x20000 | 0x500000 | Flipper |
| ota_1 | 0x520000 | 0x500000 | Bruce |
| spiffs | 0xA20000 | 0x5C0000 | Bruce |
| coredump | 0xFE0000 | 0x20000 | Diagnostics |

`patchBruce.py` applies the minimum Bruce menu patch and copies this same partition table to the Bruce PlatformIO checkout.

## Building locally

Requires ESP-IDF 5.4.1 and PlatformIO. The build script accepts the full location of `export.sh` via `ESP_IDF_EXPORT_SCRIPT` and auto-detects `~/esp/esp-idf-v5.4.1/export.sh`.

```bash
git switch flipper-v2-dualboot-latest
python3 -m unittest discover -s tests -p test_multiboot_v2.py -v
bash ./buildAndFlash_T-Embed.sh --build-only
```

A local flash command remains available once validated:
```bash
bash ./buildAndFlash_T-Embed.sh --port /dev/ttyACM0
```

## Standalone flasher (no ESP-IDF on the receiving computer)

A successful **Dual-boot full firmware** GitHub Actions run should produce a bundle
containing `bootloader/bootloader.bin`, `partition_table/partition-table.bin`,
`furi_esp32.bin`, `bruce.bin`, `manifest.json`, and `flash_dualboot.py`.
It is a **both-firmware** flasher, never a Flipper-only image.

```bash
python3 -m pip install esptool
python3 flash_dualboot.py --bundle . --dry-run
python3 flash_dualboot.py --bundle . --port /dev/ttyACM0
```

The flasher checks expected partition offsets, image sizes, ESP image magic and optional SHA256 manifest before touching flash. It flashes both apps and erases the OTA-selection sector so Flipper starts first. It does **not** erase NVS or SPIFFS contents.

**Do not flash before the build and on-device tests pass.**

## Compatibility and updates

Sor3nt's ordinary firmware OTA updater writes to the alternate application slot.
That slot is **Bruce** in this build, so standard Flipper firmware OTA (including
USB-RPC OTA) is deliberately disabled. SD-card update and non-firmware services
are preserved. Do **not** use official web-flasher or qT-Embed *Full Flash* on this
layout; those packages overwrite the bootloader/partition map and can destroy
Bruce. Bruce's standalone updater likewise must not replace the dual-boot setup.

Keep a complete SD-card backup and a known-good serial recovery method available.
The old stable branch `restore-bruce-dual-boot` remains unchanged.
