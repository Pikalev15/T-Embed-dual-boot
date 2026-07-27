> [!CAUTION]
> ## Security warning
>
> Do not flash unofficial firmware distributed under the names **L15Dev** or **Bitwire**. Use source you can inspect and builds produced from a trusted checkout of this repository.

> [!WARNING]
> This branch is experimental. The current dual-boot implementation has not yet been tested on the author’s physical T-Embed because the board is still in transit. A successful build does not guarantee that display, input, rebooting, OTA selection, power management, or every application will work on real hardware.

# T-Embed Dual Boot

Dual-boot firmware for the **LilyGo T-Embed CC1101**, combining:

- the [Flipper Zero ESP32 Port](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port)
- [Bruce](https://github.com/BruceDevices/firmware)

The Flipper-style firmware runs from `ota_0`. Bruce runs from `ota_1`. Each firmware contains an entry that selects the other OTA slot and restarts the device.

![T-Embed](pic1.jpg)

## Branch status

This README describes the `restore-bruce-dual-boot` branch.

| Area | Status |
|---|---|
| Latest main-branch Flipper-port changes included | Yes |
| Automatic Bruce clone/update and patching | Implemented |
| Both firmware images built by one script | Implemented |
| Flipper → Bruce switching code | Implemented |
| Bruce → Flipper switching code | Implemented |
| Flipper-side confirmation and visible errors | Implemented |
| Dual Boot Info menu | Work in progress |
| Current branch-tip build verification | Required |
| Real-hardware dual-boot test | Not completed |
| Recovery boot gesture | Not implemented |
| Release-ready | No |

Do not treat this branch as guaranteed or production-ready until it has been built from a clean checkout and tested through several complete Flipper → Bruce → Flipper cycles on real hardware.

## What this branch adds

### One-command dual build

`buildAndFlash_T-Embed.sh`:

1. clones Bruce into `multi-boot/bruce` when missing
2. resets and updates the Bruce checkout
3. applies `tools/bruce_multiboot.patch`
4. copies the shared 16 MB partition table into Bruce
5. builds Bruce with PlatformIO
6. builds the Flipper Zero ESP32 Port with ESP-IDF
7. flashes the Flipper firmware to `ota_0`
8. flashes Bruce to `ota_1`

Bruce is built before the ESP-IDF environment is loaded so the PlatformIO and ESP-IDF Python environments do not interfere with each other.

### Switching from the Flipper port

Open the lock menu and select **Switch to Bruce**.

The current implementation:

- checks that `ota_1` contains a valid ESP application image
- asks for confirmation before restarting
- selects `ota_1` with `esp_ota_set_boot_partition()`
- displays an error if Bruce is missing or the OTA selection fails
- reboots into Bruce after a successful selection

### Switching from Bruce

The Bruce patch adds a **Flipper Zero** entry to Bruce’s main menu. Selecting it sets `ota_0` as the next boot partition and restarts the device.

## Supported hardware

### Dual boot target

| Board | MCU | Flash | Display | Input | Main peripherals |
|---|---|---:|---|---|---|
| **LilyGo T-Embed CC1101** | ESP32-S3 | 16 MB | ST7789 320×170 | Rotary encoder + button | CC1101, PN532, IR, SD card |

The dual-boot layout in this branch is designed specifically for the 16 MB T-Embed CC1101. Do not use the same partition table unchanged on boards with a different flash size or layout.

### Other boards

The underlying Flipper Zero ESP32 Port also supports additional ESP32 boards. Those boards are not targets of this dual-boot implementation and should continue using their board-specific single-firmware build scripts.

## Flash layout

`partitions_multiboot.csv` defines the layout shared by both firmware builds:

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| `nvs` | `0x9000` | `0x6000` | Non-volatile settings |
| `otadata` | `0xF000` | `0x2000` | Selected OTA slot |
| `phy_init` | `0x11000` | `0x1000` | Radio calibration data |
| `ota_0` | `0x20000` | `0x500000` | Flipper Zero ESP32 Port |
| `ota_1` | `0x520000` | `0x500000` | Bruce |
| `spiffs` | `0xA20000` | `0x5C0000` | Bruce runtime storage |
| `coredump` | `0xFE0000` | `0x20000` | Crash dump storage |

During a complete dual-boot flash, the script erases `otadata` so the bootloader initially starts `ota_0`.

## Prerequisites

The current dual-boot script is intended for Linux or macOS.

Required:

- Git
- Python 3
- **ESP-IDF v5.4.1**
- PlatformIO
- several gigabytes of free space for toolchains and build output

Expected ESP-IDF export script:

```text
~/esp/esp-idf/export.sh
```

Set `ESP_IDF_EXPORT_SCRIPT` when ESP-IDF is installed elsewhere.

The build script searches for PlatformIO using:

```text
pio
platformio
~/.platformio/penv/bin/pio
python3 -m platformio
```

## Clone and select the branch

```bash
git clone https://github.com/Pikalev15/T-Embed-dual-boot.git
cd T-Embed-dual-boot
git checkout restore-bruce-dual-boot
```

For an existing checkout:

```bash
cd ~/Projects/T-Embed-dual-boot
git checkout restore-bruce-dual-boot
git pull --ff-only
```

## Build without a board

A connected T-Embed is not required for a build-only check:

```bash
./buildAndFlash_T-Embed.sh --build-only
```

The expected ending is:

```text
Build complete (--build-only). Nothing flashed.
```

A successful build confirms that the source compiles and that both application images fit their configured partitions. It does not confirm that the firmware behaves correctly on hardware.

## Build and flash

After the board arrives and is connected:

```bash
./buildAndFlash_T-Embed.sh
```

The script attempts to detect:

```text
/dev/ttyACM*
/dev/cu.usbmodem*
```

Specify the port manually when needed:

```bash
./buildAndFlash_T-Embed.sh --port /dev/ttyACM0
```

Build, flash, and open the serial monitor:

```bash
./buildAndFlash_T-Embed.sh --port /dev/ttyACM0 --monitor
```

Build or flash only the Flipper-port firmware without updating or flashing Bruce:

```bash
./buildAndFlash_T-Embed.sh --skip-bruce
```

Show all options:

```bash
./buildAndFlash_T-Embed.sh --help
```

> [!IMPORTANT]
> The browser flasher has not been validated for this experimental dual-boot branch. Use the repository script until a branch-specific flash sequence has been tested.

## First hardware test plan

After the board arrives:

1. flash the complete dual-boot image
2. confirm that the device initially boots the Flipper port
3. confirm that the display and rotary input work
4. open the lock menu
5. choose **Switch to Bruce**
6. confirm the restart dialog
7. verify that Bruce boots and accepts input
8. choose **Flipper Zero** inside Bruce
9. verify that the Flipper port boots again
10. repeat the cycle several times
11. test SD-card access from both firmware images
12. test power-off, reset, and deep sleep

Capture serial logs for any boot loop, crash, blank screen, or failed OTA switch.

## Recovery and risk

There is currently no hold-button recovery selector or automatic crash fallback. A broken firmware image may require putting the ESP32-S3 into download mode and reflashing it over USB.

Before a public release, the project should add:

- a boot-time recovery gesture that forces `ota_0`
- repeated-boot-failure detection
- automated clean builds for both firmware projects
- binary-size checks against both OTA partition limits
- tested release binaries with documented flash offsets

## Flipper-port overview

The Flipper Zero ESP32 Port included in this repository provides a broad collection of hardware-control, storage, connectivity, scripting, utility, and game applications. Availability depends on the board and attached peripherals.

Main areas include:

- Sub-GHz tools through the onboard CC1101
- infrared learning and remote control
- Wi-Fi and Bluetooth diagnostics and research tools
- NFC support through PN532
- SD-card archive and file management
- USB mass-storage and desktop connectivity
- JavaScript applications and plugins
- system settings, power controls, clock, and device information
- games including Doom, Snake, and Roulette

Use radio, networking, NFC, USB, and automation features only on devices and systems you own or have explicit permission to test.

## SD card

Many applications require assets on a FAT32-formatted SD card. A starter package is included as `sdcard.zip`.

Common paths include:

| Path | Purpose |
|---|---|
| `/ext/Manifest` | Asset-pack presence check |
| `/ext/dolphin/` | Desktop animations |
| `/ext/apps_assets/nfc/plugins/` | NFC protocol plugins |
| `/ext/apps_data/nfc/plugins/` | NFC card-parser plugins |
| `/ext/apps_data/js_app/plugins/` | JavaScript module bindings |
| `/ext/apps_data/doom/doom1.wad` | Doom data file |
| `/ext/badusb/` | User automation scripts and keyboard layouts |
| `/ext/infrared/assets/` | Universal remote databases |
| `/ext/nfc/assets/` | NFC assets |
| `/ext/subghz/assets/` | Sub-GHz assets |
| `/ext/wifi/` | Saved network data and captures |

## Important project files

| File | Purpose |
|---|---|
| `buildAndFlash_T-Embed.sh` | Build and flash both firmware images |
| `patchBruce.py` | Clone, update, reset, and patch Bruce |
| `tools/bruce_multiboot.patch` | Add the return-to-Flipper menu item to Bruce |
| `partitions_multiboot.csv` | Shared 16 MB dual-boot layout |
| `sdkconfig.defaults.esp32s3` | ESP32-S3 build defaults |
| `interface.html` | Browser flasher UI; dual-boot flow not yet validated |
| `multi-boot/bruce/` | Local Bruce checkout generated during the build |

## Updating Bruce

`patchBruce.py` is designed to be safe to run before each build. It:

- resets the local Bruce checkout
- attempts a fast-forward update
- reapplies the dual-boot patch
- copies the shared partition table

If upstream Bruce changes the affected menu code, patching exits with an error instead of silently producing a build without the return-to-Flipper entry. Update `tools/bruce_multiboot.patch` before continuing.

## Known limitations

- real-device switching has not yet been verified on this branch
- the current branch tip still needs a fresh build-only verification
- the Dual Boot Info menu entry is unfinished
- there is no boot-time recovery selector
- there is no automatic rollback after repeated boot failures
- upstream changes can break the Bruce patch or firmware compatibility
- features depending on external hardware, board-specific pins, or SD assets require separate testing
- encrypted manufacturer Sub-GHz keystores that depend on unavailable Flipper enclave keys are not supported

## MVP completion checklist

The MVP is complete only after:

- both firmware projects build from a clean checkout
- both images flash at the intended offsets
- both firmware images boot on the T-Embed CC1101
- switching works in both directions
- confirmation and error dialogs work on-device
- SD-card data remains accessible
- power-off and deep sleep still work
- a practical recovery path is documented and tested

Later improvements can include a boot selector, forced-recovery gesture, crash counter, separate firmware updates, branch-specific web flashing, release packaging, and automated builds.

## Credits

- [Sor3nt/Flipper-Zero-ESP32-Port](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port)
- [BruceDevices/firmware](https://github.com/BruceDevices/firmware)
- the Flipper Zero firmware contributors
- contributors to the ESP32 board ports and applications included in this repository

## Community

- [Flipper Zero meets ESP32 Discord](https://discord.gg/5DnAqFXaBC)

## Disclaimer

This project is provided for education, research, interoperability, and authorised testing. You are responsible for complying with local laws and for using its hardware and connectivity features only on devices and systems you own or have explicit permission to test. The contributors are not responsible for damaged hardware, lost data, service disruption, or unlawful use.
