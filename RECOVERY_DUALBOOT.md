# T-Embed CC1101 dual-boot: recovery and diagnosis

**Status:** Source fixes under test. Do not assume a green CI build proves either OS boots on the physical device.

## Switch between operating systems

- Flipper: **OK -> Main menu -> Bruce OS** (uses the native icon menu); confirm the reboot.
  The entry appears only if an OTA_1 firmware header is readable and the exact dual-boot layout is present.
  `esp_ota_set_boot_partition` performs final image validation before restarting.
- Bruce: **Flipper Zero -> Reboot to Flipper**. The patched Bruce entry also checks the OTA_0 image.
- The older Flipper lock-menu shortcut remains as a recovery fallback.

These checks protect against missing/corrupt images. They **cannot detect a runtime crash**, bad PSRAM, SD filesystem, or a bootloop once the target OS starts.

## If Bruce fails to start but Flipper boots

1. Stop trying to switch repeatedly. Do not flash the official single-OS image over the dual-boot partition table.
2. Unplug the SD card and try one reboot. This is a non-destructive way to isolate an SD initialization or malformed-file problem.
3. Capture a serial log during the reboot using your device's detected port.
   On Linux/macOS, with the optional `pyserial` package installed:

   ```sh
   python3 -m serial.tools.miniterm /dev/ttyACM0 115200
   ```

   Press RESET while the monitor is open, select Bruce from Flipper, and save everything from the first bootloader line through the crash. If your firmware owns the USB serial interface, disconnect and reconnect after resetting. The port can change.

4. Look for `invalid image`, `invalid header`, `No bootable app partitions`, `Guru Meditation`, `PSRAM`, `SPIFFS`, or repeated watchdog messages. Share the **actual log**, rather than assuming the cause.

## If stuck trying to boot Bruce (the last OTA slot)

Before changing firmware images, you can make the bootloader use Flipper's OTA_0 default by **only** erasing the 8 KiB OTA-selection partition, assuming the dual-boot partition map and OTA_0 image are valid:

```sh
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 erase-region 0xF000 0x2000
```

For older esptool versions, the subcommand may be spelled `erase_region`. **Do not** erase the whole flash as a first step. This resets the boot choice but preserves Flipper, Bruce, NVS, and SPIFFS.

## If an official single-OS image also bootloops

This does **not** prove the dual-boot code caused it. Check bootloader/partition compatibility, USB supply, SD removal, previous flash mode/settings, and the reset reason in the serial log.

Only as a last resort, after backing up settings and accepting data loss, use a full `erase_flash` / `erase-flash` followed by a clean *matched* official firmware bundle. That erases **all** internal flash data and overwrites the custom dual-boot layout.

**Do not erase NVS or SPIFFS to troubleshoot Bruce before backing them up.** Bruce stores data in internal filesystem partitions and may be using retained settings.

## CI and hardware release criteria

- Build both images against the identical 16MB partition table.
- Validate image SHA256 and 5 MiB application slot limits.
- Test first boot into Flipper, switch to Bruce, and return to Flipper.
- Reboot each OS twice, with and without an SD card.
- Check full USB/PSRAM/CC1101 initialization, then save a serial trace for any crash.

The experimental branch is not a hardware-verified repair.
