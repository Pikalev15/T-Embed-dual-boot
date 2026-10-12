# Flipper v2.0.0 + Bruce 1.16.1 (T-Embed CC1101 / CC1101 Plus)

This branch starts from **Sor3nt Flipper ESP32 port v2.0.0** and adds only two-way boot switching.
It builds Bruce from pinned upstream **1.16.1** (ba519c936c87b89c9c667c15b932d7faca5360d5).

## Partition contract (16 MB)
- ota_0: Flipper, 0x20000, 5 MiB
- ota_1: Bruce, 0x520000, 5 MiB
- spiffs: shared Bruce scratch, 0xA20000, 0x5C0000
- NVS + otadata remain at their previous multi-boot offsets.

Both firmware images must use `partitions_multiboot.csv`. The builder applies the same file to Bruce's `custom_16Mb.csv`.

## How to build

ESP-IDF 5.4.1 and PlatformIO are required. To override the IDF path, use
`ESP_IDF_EXPORT_SCRIPT=/path/to/export.sh`.

```bash
./buildAndFlash_T-Embed.sh --build-only
./buildAndFlash_T-Embed.sh --port /dev/ttyACM0
```

Flipper: open the lock menu and select **Switch to Bruce**.
Bruce: open **Flipper Zero** on the main menu, then select **Reboot to Flipper**.

## Important: updater compatibility

Upstream Flipper v2 supports WiFi OTA and USB-RPC OTA by writing the *other* OTA slot.
In this dual-boot build that slot is occupied by Bruce. The shared `fw_ota.c` code
rejects OTA firmware installation whenever the multiboot SPIFFS partition exists.
Other v2 features, SD syncing, streaming, and USB-RPC functions are retained.
Do **not** run an official Flipper webflasher, qT-Embed **Full flash**, or Bruce firmware
updater over this dual-boot image: they can rewrite the partition table or replace the
other firmware. Updating these two images requires rebuilding both here.
Backup your SD card before first flashing.

## Status

Source integration only. Not full-compiled or device-tested yet.
Confirm both application binaries are **at most 0x500000 bytes**, and verify both
switch directions on actual hardware before treating the release as stable.

The earlier custom standalone selector, staging updater, and skip-once NVS logic
are intentionally not carried forward; two-way menu switching is the core design.
