#!/usr/bin/env python3
"""Build a release manifest by reading the image it describes.

Every field the device compares against the binary — product, version,
secure_version — is taken OUT of the binary's own app descriptor rather than
passed in on the command line. That is the whole point. hk_ota_image_check()
refuses an update when the manifest and the image disagree, and the cheapest
way to guarantee a legitimate release never trips that check is to make
disagreement impossible to express: there is no parameter here that could be
typed wrongly.

What is passed in is checked FOR agreement instead: the git tag must match the
version compiled into the image, so a v0.3.0 tag cannot publish a 0.2.0 build.

Usage:
    make_manifest.py --image build/harman-kardom.bin \
                     --tag v0.2.0 \
                     --asset-url https://github.com/o/r/releases/download/v0.2.0/hk.bin \
                     --channel stable \
                     --hw-revision prototype-n16r8 \
                     --slot-size 0x6e0000
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

# esp_image_header_t (24 B) is followed by esp_image_segment_header_t (8 B),
# and the app descriptor is the first thing in that segment's data. So it starts
# at 32, not 24 — a mistake worth naming, because reading at 24 yields a
# plausible-looking struct full of the wrong values rather than an obvious
# failure. ESP-IDF asserts the struct sizes in esp_app_format.h/esp_app_desc.h.
IMAGE_HEADER_SIZE = 24
SEGMENT_HEADER_SIZE = 8
APP_DESC_OFFSET = IMAGE_HEADER_SIZE + SEGMENT_HEADER_SIZE
APP_DESC_SIZE = 256
APP_DESC_MAGIC = 0xABCD5432
ESP32S3_CHIP_ID = 0x0009


class ImageError(Exception):
    """The binary is not an image this script is willing to describe."""


def read_app_desc(path: Path) -> dict:
    """Pull the fields worth publishing out of an ESP-IDF app image."""
    raw = path.read_bytes()
    if len(raw) < APP_DESC_OFFSET + APP_DESC_SIZE:
        raise ImageError(f"{path} is too small to be an app image")

    # esp_image_header_t: magic, segment_count, spi_mode, spi_speed/size,
    # entry_addr, wp_pin, spi_pin_drv[3], chip_id, ...
    if raw[0] != 0xE9:
        raise ImageError(f"{path} does not start with the 0xE9 image magic")
    chip_id = struct.unpack_from("<H", raw, 12)[0]
    if chip_id != ESP32S3_CHIP_ID:
        raise ImageError(
            f"{path} is built for chip_id 0x{chip_id:04x}, not ESP32-S3 "
            f"(0x{ESP32S3_CHIP_ID:04x})"
        )

    desc = raw[APP_DESC_OFFSET:APP_DESC_OFFSET + APP_DESC_SIZE]
    magic, secure_version = struct.unpack_from("<II", desc, 0)
    if magic != APP_DESC_MAGIC:
        raise ImageError(
            f"{path} has app descriptor magic 0x{magic:08x}, expected "
            f"0x{APP_DESC_MAGIC:08x}"
        )

    def field(offset: int, size: int) -> str:
        blob = desc[offset:offset + size]
        return blob.split(b"\x00", 1)[0].decode("utf-8")

    return {
        "version": field(16, 32),
        "project_name": field(48, 32),
        "secure_version": secure_version,
    }


def build_manifest(args: argparse.Namespace) -> dict:
    image = Path(args.image)
    desc = read_app_desc(image)
    payload = image.read_bytes()

    tag_version = args.tag[1:] if args.tag.startswith("v") else args.tag
    if tag_version != desc["version"]:
        raise ImageError(
            f"tag {args.tag} says {tag_version} but the image was built as "
            f"{desc['version']}. Publishing this would ship a version nobody "
            f"asked for."
        )

    if len(payload) > args.slot_size:
        raise ImageError(
            f"image is {len(payload)} bytes, larger than the "
            f"{args.slot_size}-byte OTA slot"
        )

    return {
        "product": desc["project_name"],
        "version": desc["version"],
        "channel": args.channel,
        "target": "esp32s3",
        "hw_revision": args.hw_revision,
        "asset": args.asset_url,
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "secure_version": desc["secure_version"],
        "min_updater_version": args.min_updater_version,
    }


def parse_size(text: str) -> int:
    return int(text, 0)


#: OTA slot size per board, taken from that board's partition table. The manifest
#: refuses an image that cannot fit the slot it is destined for, and "the slot" is
#: a property of the board -- so it is looked up from the hardware revision rather
#: than defaulted to the product's. A devkit image checked against the product's
#: 0x6e0000 slot would pass a gate 76% larger than the flash it is going to.
BOARD_SLOT_SIZE = {
    "prototype-n16r8": 0x6E0000,   # firmware/partitions.csv
    "devkit-n8r2": 0x2E0000,       # firmware/partitions-devkit.csv
}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--asset-url", required=True)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--hw-revision", default="prototype-n16r8")
    parser.add_argument("--min-updater-version", default="0.1.0")
    parser.add_argument("--slot-size", type=parse_size, default=None,
                        help="OTA slot the image must fit; defaults to the slot of "
                             "the board named by --hw-revision")
    parser.add_argument("--out")
    args = parser.parse_args(argv)

    known = BOARD_SLOT_SIZE.get(args.hw_revision)
    if args.slot_size is None:
        if known is None:
            print(f"error: --hw-revision {args.hw_revision!r} is not a board this "
                  f"script knows a slot size for; pass --slot-size explicitly or add "
                  f"the board to BOARD_SLOT_SIZE", file=sys.stderr)
            return 1
        args.slot_size = known
    elif known is not None and args.slot_size != known:
        # Both were given and they disagree. One of them is wrong and there is no
        # way to tell which, so neither is used.
        print(f"error: --slot-size 0x{args.slot_size:x} does not match the "
              f"0x{known:x} slot of {args.hw_revision}", file=sys.stderr)
        return 1

    try:
        manifest = build_manifest(args)
    except ImageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
