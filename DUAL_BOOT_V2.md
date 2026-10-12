# Flipper 2.0.0 + Bruce 1.16.1 dual-boot design

This experimental branch starts at Sor3nt's **v2.0.0 Git tag** (not an
old 1.x source tree), with [BruceDevices/firmware] pinned to the **1.16.1
tag commit `ba519c936c87b89c9c667c15b932d7faca5360d5`**.

## What upstream features survive

The Flipper firmware and all source components come from Sor3nt 2.0.0
(including USB/qT-Embed support, Wi-Fi and other upstream apps). Bruce
is built from official 1.16.1. No legacy two-stage dual-firmware updater,
custom Bruce payload handler, or duplicate Flipper dispatcher is imported.

Only the following are added:
- Shared 16 MB app layout, fixed Flipper `ota_0`, Bruce `ota_1`.
- Flipper Desktop's 4-second startup selector (defaults to Flipper).
- A `Switch to Bruce` action in the Flipper lock menu, only when the
  Bruce partition contains a valid ESP app.
- A single `Flipper Zero → Reboot to Flipper` action in Bruce. It writes
  a one-shot NVS key `dual_boot/skip_once` to enter Flipper directly.
- Bruce arms Flipper `ota_0` as the next reset target without restarting.
  Normal resets from Bruce therefore return to the selector.
- Crash/watchdog fallback skips the selector to reduce boot-loop risk.

## Partition compatibility

```
0x009000 nvs       0x006000
0x00f000 otadata   0x002000
0x011000 phy_init  0x001000
0x020000 ota_0     0x500000  Flipper 2.0.0
0x520000 ota_1     0x500000  Bruce 1.16.1
0xa20000 spiffs    0x5c0000  Bruce LittleFS
0xfe0000 coredump  0x020000
```

This table is **not** the same as Sor3nt's new v2 16 MB dual-OTA
update table: their ota_1 is normally a spare Flipper image, whereas
our ota_1 contains Bruce. Therefore:
- Flipper's `fw_ota` updater is explicitly disabled on our layout and
  refuses OTA write requests before erasing Bruce.
- Sor3nt's unmodified OTA downloads should **not** be enabled in this
  branch. A future updater must validate firmware type and target slot.
- qT-Embed SD management/display streaming can remain, but firmware
  flashing, especially **Full flash**, must not use upstream partition
  assumptions. Reflash with the dual-boot script instead.
- For SD files, merge Flipper **v2.0.0** `sdcard.zip` contents with
  existing Bruce resources; back up conflicts rather than overwrite
  Bruce's configs.

## Building (once dependencies are installed)

For Linux/ChromeOS Crostini: ESP-IDF, PlatformIO, git, and a USB serial
port are needed; the script does not auto-install gigabytes of tools.

```bash
git fetch origin
git switch flipper-v2-dualboot-rework
git pull --ff-only
./buildAndFlash_T-Embed.sh --build-only
# Only after a clean build and reviewing any local data/SD backups:
./buildAndFlash_T-Embed.sh --port /dev/ttyACM0
```

The script accepts `ESP_IDF_EXPORT_SCRIPT` to point to a custom
ESP-IDF installation, and detects `~/esp/esp-idf-v5.4.1` as a fallback.
For Flipper-only builds, use `--skip-bruce` (does not update ota_1).

## Validation gate

This branch is **not release-ready** until:
1. Bruce patch applies cleanly to the pinned 1.16.1 checkout.
2. Full Flipper ESP-IDF and Bruce PlatformIO builds fit the app slots.
3. Boot selector chooses both OSes, and times out to Flipper.
4. Bruce explicit Flipper action bypasses selector once; normal reset
   from Bruce returns to selector.
5. Crash/watchdog fallback, SD mounting, radio peripherals and qT-Embed
   non-flashing functions are tested on physical CC1101 Plus.
6. Attempts to invoke Flipper's stock OTA are blocked without erasing Bruce.

`restore-bruce-dual-boot` and old `boot-selector` remain untouched.
