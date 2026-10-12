"""Guard against unsupported uncompressed writes in the published browser flasher."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PAGE = ROOT / "pages" / "index.html"

class PagesFlasherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = PAGE.read_text(encoding="utf-8")

    def test_never_use_unsupported_uncompressed_mode(self):
        self.assertNotIn("compress:false", self.html)
        self.assertNotIn("compress: false", self.html)

    def test_boot_selection_is_final_compressed_image(self):
        self.assertIn('...prepared.map(x=>({address:x.address,data:binary(x.data)}))', self.html)
        self.assertIn('{address:0xf000,data:"\\xFF".repeat(0x2000)}', self.html)
        self.assertIn("const flashFiles=[", self.html)
        self.assertEqual(self.html.count("await loader.writeFlash("), 1)
        self.assertIn("fileArray:flashFiles", self.html)
        self.assertIn("compress:true", self.html)

    def test_validate_both_images_and_partitions(self):
        for name in ("bruce.bin", "furi_esp32.bin", "partition-table.bin", "manifest.json"):
            self.assertIn(name, self.html)
        self.assertIn("validatePartitions(bytes)", self.html)
        self.assertIn("crypto.subtle.digest", self.html)

if __name__ == "__main__":
    unittest.main()
