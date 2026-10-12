#!/usr/bin/env python3
"""Static invariants for the Sor3nt v2 + Bruce 1.16.1 dual-boot port."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class MultiBootV2Tests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_partition_addresses_and_sizes(self):
        rows = {}
        for line in self.read("partitions_multiboot.csv").splitlines():
            if not line or line.lstrip().startswith("#"):
                continue
            row = [x.strip() for x in line.split(",")]
            rows[row[0]] = (int(row[3], 0), int(row[4], 0))
        self.assertEqual(rows["nvs"], (0x9000, 0x6000))
        self.assertEqual(rows["otadata"], (0xF000, 0x2000))
        self.assertEqual(rows["ota_0"], (0x20000, 0x500000))
        self.assertEqual(rows["ota_1"], (0x520000, 0x500000))
        self.assertEqual(rows["spiffs"], (0xA20000, 0x5C0000))
        self.assertEqual(rows["coredump"], (0xFE0000, 0x20000))
        spans = sorted((start, start + size, name) for name, (start, size) in rows.items())
        for (_, a_end, a_name), (b_start, _, b_name) in zip(spans, spans[1:]):
            self.assertLessEqual(a_end, b_start, f"{a_name} overlaps {b_name}")
        self.assertLessEqual(spans[-1][1], 0x1000000)

    def test_flipper_writes_bruce_only_by_switch(self):
        menu = self.read("applications/services/desktop/scenes/desktop_scene_lock_menu.c")
        view = self.read("applications/services/desktop/views/desktop_view_lock_menu.c")
        events = self.read("applications/services/desktop/views/desktop_events.h")
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_1", menu)
        self.assertIn("esp_ota_get_partition_description", menu)
        self.assertIn("esp_ota_set_boot_partition(part)", menu)
        self.assertIn("Switch to Bruce", view)
        self.assertIn("DesktopLockMenuEventBruce", events)

    def test_ota_cannot_erase_bruce(self):
        src = self.read("components/fw_ota/fw_ota.c")
        self.assertIn("ESP_PARTITION_SUBTYPE_DATA_SPIFFS", src)
        self.assertIn("return !fw_ota_is_multiboot()", src)
        self.assertRegex(src, r"bool fw_ota_flash_file[\\s\\S]*?if\\(!fw_ota_is_supported\\(\\)\\)")
        self.assertIn("Flipper OTA disabled in Bruce dual-boot layout", src)

    def test_bruce_only_minimal_return_menu(self):
        preparer = self.read("patchBruce.py")
        patch = self.read("tools/bruce_multiboot.patch")
        self.assertIn("ba519c936c87b89c9c667c15b932d7faca5360d5", preparer)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_0", patch)
        self.assertIn("esp_ota_set_boot_partition(target)", patch)
        self.assertNotIn("DualBootUpdater", patch)
        self.assertNotIn("skip_once", patch)
        self.assertNotIn("dualBootUpdaterResumeIfPending", preparer)

    def test_toolchain_partition_config(self):
        self.assertIn("partitions_multiboot.csv", self.read("sdkconfig.defaults.esp32s3"))
        self.assertIn("partitions_multiboot.csv", self.read("sdkconfig"))
        script = self.read("buildAndFlash_T-Embed.sh")
        self.assertIn('partition_offset ota_1', script)
        self.assertIn('0x500000', script)
        self.assertIn('patchBruce.py', script)


if __name__ == "__main__":
    unittest.main()
