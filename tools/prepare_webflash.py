#!/usr/bin/env python3
"""Create a static GitHub Pages bundle for the T-Embed dual-boot flasher.

The firmware projects must already have been built with:

    ./buildAndFlash_T-Embed.sh --build-only

The output directory contains the web UI plus the four binaries required for a
clean 16 MB T-Embed CC1101 installation. The generated manifest records sizes,
offsets and SHA-256 hashes so the browser can verify every download before it
writes flash.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = REPO_ROOT / "_site"
FIRMWARE_RELATIVE_DIR = Path("firmware/t-embed-dual/latest")

PARTS = (
    {
        "name": "bootloader",
        "source": REPO_ROOT / "build_t_embed/bootloader/bootloader.bin",
        "filename": "bootloader.bin",
        "offset": 0x000000,
        "max_size": 0x008000,
    },
    {
        "name": "partition-table",
        "source": REPO_ROOT / "build_t_embed/partition_table/partition-table.bin",
        "filename": "partition-table.bin",
        "offset": 0x008000,
        "max_size": 0x001000,
    },
    {
        "name": "flipper",
        "source": REPO_ROOT / "build_t_embed/furi_esp32.bin",
        "filename": "flipper.bin",
        "offset": 0x020000,
        "max_size": 0x500000,
    },
    {
        "name": "bruce",
        "source": REPO_ROOT
        / "multi-boot/bruce/.pio/build/lilygo-t-embed-cc1101/firmware.bin",
        "filename": "bruce.bin",
        "offset": 0x520000,
        "max_size": 0x500000,
    },
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision() -> str:
    env_sha = os.environ.get("GITHUB_SHA")
    if env_sha:
        return env_sha

    try:
        return subprocess.check_output(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def require_inputs() -> None:
    missing = [str(part["source"]) for part in PARTS if not part["source"].is_file()]
    if missing:
        joined = "\n  - ".join(missing)
        raise SystemExit(
            "Missing build output(s):\n  - "
            + joined
            + "\nRun ./buildAndFlash_T-Embed.sh --build-only first."
        )


def validate_size(name: str, path: Path, max_size: int) -> int:
    size = path.stat().st_size
    if size <= 0:
        raise SystemExit(f"{name} is empty: {path}")
    if size > max_size:
        raise SystemExit(
            f"{name} is too large: {size:#x} bytes; limit is {max_size:#x} bytes"
        )
    return size


def build_site(output: Path) -> None:
    require_inputs()

    page_source = REPO_ROOT / "dual-boot.html"
    if not page_source.is_file():
        raise SystemExit(f"Missing browser flasher page: {page_source}")

    if output.exists():
        shutil.rmtree(output)

    firmware_dir = output / FIRMWARE_RELATIVE_DIR
    firmware_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(page_source, output / "index.html")
    (output / ".nojekyll").write_text("", encoding="utf-8")

    for optional_asset in ("favicon.ico", "pic1.jpg"):
        source = REPO_ROOT / optional_asset
        if source.is_file():
            shutil.copy2(source, output / optional_asset)

    manifest_parts: list[dict[str, object]] = []
    total_size = 0

    for part in PARTS:
        source = part["source"]
        size = validate_size(str(part["name"]), source, int(part["max_size"]))
        destination = firmware_dir / str(part["filename"])
        shutil.copy2(source, destination)
        total_size += size

        manifest_parts.append(
            {
                "name": part["name"],
                "file": part["filename"],
                "offset": int(part["offset"]),
                "offset_hex": f"0x{int(part['offset']):06x}",
                "size": size,
                "sha256": sha256(destination),
            }
        )

    generated_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    manifest = {
        "schema_version": 1,
        "name": "T-Embed CC1101 Dual Boot",
        "board": "LilyGo T-Embed CC1101",
        "chip": "ESP32-S3",
        "flash_size": "16MB",
        "flash_mode": "dio",
        "flash_frequency": "80m",
        "erase_all": True,
        "default_slot": "ota_0",
        "ota_0": "Flipper Zero ESP32 Port",
        "ota_1": "Bruce",
        "generated_at": generated_at,
        "commit": git_revision(),
        "total_binary_bytes": total_size,
        "parts": manifest_parts,
    }

    manifest_path = firmware_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Prepared browser-flasher site: {output}")
    for part in manifest_parts:
        print(
            f"  {part['offset_hex']}  {part['name']:<16} "
            f"{part['size']:>8} bytes  {part['sha256']}"
        )
    print(f"  manifest: {manifest_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"site output directory (default: {DEFAULT_OUTPUT})",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output = args.output.expanduser().resolve()
    build_site(output)


if __name__ == "__main__":
    main()
