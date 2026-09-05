#!/usr/bin/env python3
"""Validate the Harman Kardom partition table against the layout the project needs.

ESP-IDF checks that a partition table is syntactically valid and fits the flash.
It does not check the things this project actually depends on: two equally sized
OTA slots, calibration that a user reset cannot reach, and enough headroom that a
firmware growing by a megabyte does not quietly stop fitting.

    python3 firmware/tools/check_partitions.py
    python3 firmware/tools/check_partitions.py --app-size build/harman-kardom.bin

With --app-size the script also acts as the CI size gate: it fails when the built
image no longer leaves the required free margin in its slot.

Exit code 0 means every rule holds. Nothing here says anything about a physical
gate; it is a layout check.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CSV = ROOT / "partitions.csv"

#: ADR-0010 locks the product board to a 16 MB part. The bring-up devkit has 8 MB
#: and its own table (partitions-devkit.csv); pass --flash-size to check that one.
#: The default stays 16 MB so every existing caller keeps checking the product
#: layout without being edited.
FLASH_SIZE = 16 * 1024 * 1024

#: An app partition offset must be aligned to this, a hardware requirement of
#: the ESP32-S3 MMU.
APP_ALIGNMENT = 0x10000

#: ESP-IDF defines the OTA data partition as exactly two sectors.
OTADATA_SIZE = 0x2000

#: Fraction of an OTA slot that must stay free after a build. A slot that is
#: nearly full leaves no room for the audio stack that F1-F3 will add, and the
#: point of catching it in CI is to hear about it before a release is cut.
REQUIRED_FREE_FRACTION = 0.30

#: Lowest offset a partition may occupy. Below this sit the second-stage
#: bootloader (from 0x0) and the partition table itself (0x8000). A partition
#: placed there overwrites one of them, and the resulting board does not boot.
FIRST_USABLE_OFFSET = 0x9000

#: Partitions the documented design cannot work without: the required
#: (type, subtype) and why it has to be there.
#:
#: The type and subtype are checked, not just the name. The bootloader and the
#: esp_partition API find a partition by type and subtype and ignore the label
#: entirely, so a row named `ota_1` that is not actually subtype `ota_1` leaves
#: the device with a single usable slot while looking perfectly correct here.
REQUIRED = {
    "nvs": ("data", "nvs", "user settings"),
    "otadata": ("data", "ota", "OTA slot selection (ADR-0008)"),
    "factory_cal": ("data", "nvs",
                    "driver protection calibration a user reset must not erase (PRD-008)"),
    "ota_0": ("app", "ota_0", "application slot A"),
    "ota_1": ("app", "ota_1", "application slot B"),
}

#: ESP-IDF stores a partition label in a 17-byte field, so 16 characters is the
#: real limit. A longer name is silently truncated by the generator.
MAX_LABEL_LENGTH = 16


@dataclass(frozen=True)
class Partition:
    name: str
    kind: str
    subtype: str
    offset: int
    size: int
    flags: str

    @property
    def end(self) -> int:
        return self.offset + self.size


def parse_number(text: str) -> int:
    text = text.strip()
    if text.lower().endswith("k"):
        return int(text[:-1], 0) * 1024
    if text.lower().endswith("m"):
        return int(text[:-1], 0) * 1024 * 1024
    return int(text, 0)


def parse_size(text: str) -> int:
    """Accept the spellings a person actually types for a flash size.

    ``8MB``, ``8M``, ``0x800000`` and ``8388608`` all mean the same thing. A bare
    number is bytes, not megabytes: silently reading ``8`` as 8 MB would let a
    typo produce a table that passes every rule while being 8 bytes long.
    """
    cleaned = text.strip()
    if cleaned.lower().endswith("b") and not cleaned.lower().startswith("0x"):
        cleaned = cleaned[:-1]
    size = parse_number(cleaned)
    if size <= 0:
        raise ValueError(f"{text!r} is not a positive size")
    return size


def load(csv_path: Path) -> list[Partition]:
    partitions: list[Partition] = []
    for number, raw in enumerate(csv_path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise ValueError(f"{csv_path}:{number}: expected at least 5 columns, got {len(fields)}")
        partitions.append(Partition(
            name=fields[0], kind=fields[1], subtype=fields[2],
            offset=parse_number(fields[3]), size=parse_number(fields[4]),
            flags=fields[5] if len(fields) > 5 else "",
        ))
    return partitions


def check(partitions: list[Partition], app_size: int | None,
          flash_size: int = FLASH_SIZE) -> list[str]:
    problems: list[str] = []
    by_name = {p.name: p for p in partitions}

    for name, (kind, subtype, reason) in REQUIRED.items():
        partition = by_name.get(name)
        if partition is None:
            problems.append(f"missing partition {name!r}: {reason}")
            continue
        if (partition.kind, partition.subtype) != (kind, subtype):
            problems.append(
                f"{name} is ({partition.kind}, {partition.subtype}) but must be "
                f"({kind}, {subtype}): the bootloader locates it by type and subtype, "
                f"not by name -- {reason}")

    for partition in partitions:
        if len(partition.name) > MAX_LABEL_LENGTH:
            problems.append(
                f"{partition.name!r} is {len(partition.name)} characters; ESP-IDF truncates "
                f"a label to {MAX_LABEL_LENGTH}")
        if partition.offset < FIRST_USABLE_OFFSET:
            problems.append(
                f"{partition.name} starts at 0x{partition.offset:x}, below 0x{FIRST_USABLE_OFFSET:x}: "
                "it would overwrite the bootloader or the partition table")
        if partition.size <= 0:
            problems.append(f"{partition.name} has a non-positive size")

    names = [p.name for p in partitions]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        problems.append(f"duplicate partition names: {duplicates}")

    ordered = sorted(partitions, key=lambda p: p.offset)
    for earlier, later in zip(ordered, ordered[1:]):
        if earlier.end > later.offset:
            problems.append(
                f"{earlier.name} ends at 0x{earlier.end:x} but {later.name} starts at "
                f"0x{later.offset:x}: they overlap")

    if ordered and ordered[-1].end > flash_size:
        problems.append(
            f"table ends at 0x{ordered[-1].end:x}, past the {flash_size // (1024 * 1024)} MB flash")

    for partition in partitions:
        if partition.kind == "app" and partition.offset % APP_ALIGNMENT:
            problems.append(
                f"{partition.name} starts at 0x{partition.offset:x}, which is not aligned to "
                f"0x{APP_ALIGNMENT:x} as an app partition must be")

    otadata = by_name.get("otadata")
    if otadata and otadata.size != OTADATA_SIZE:
        problems.append(
            f"otadata is 0x{otadata.size:x}; ESP-IDF requires exactly 0x{OTADATA_SIZE:x}")

    subtypes = [(p.kind, p.subtype) for p in partitions if p.kind == "app"]
    duplicate_subtypes = sorted({s for s in subtypes if subtypes.count(s) > 1})
    if duplicate_subtypes:
        problems.append(
            f"two app partitions share a subtype {duplicate_subtypes}: only one of them "
            "is reachable, so A/B rollback cannot work")

    slot_a, slot_b = by_name.get("ota_0"), by_name.get("ota_1")
    if slot_a and slot_b:
        if slot_a.size != slot_b.size:
            problems.append(
                f"ota_0 is 0x{slot_a.size:x} and ota_1 is 0x{slot_b.size:x}; ADR-0008 needs "
                "equally sized slots so either image can hold a full release")
        if app_size is not None:
            free = slot_a.size - app_size
            fraction = free / slot_a.size
            if fraction < REQUIRED_FREE_FRACTION:
                problems.append(
                    f"application is {app_size} bytes, leaving {fraction:.1%} of the slot free; "
                    f"at least {REQUIRED_FREE_FRACTION:.0%} is required")

    calibration = by_name.get("factory_cal")
    if calibration and calibration.kind != "data":
        problems.append("factory_cal must be a data partition")
    if calibration and slot_a and calibration.offset > slot_a.offset:
        problems.append("factory_cal should sit before the application slots")

    return problems


def report(partitions: list[Partition], app_size: int | None,
           flash_size: int = FLASH_SIZE) -> None:
    print(f"{'name':<14}{'type':<7}{'subtype':<10}{'offset':>12}{'size':>12}{'end':>12}")
    for partition in sorted(partitions, key=lambda p: p.offset):
        print(f"{partition.name:<14}{partition.kind:<7}{partition.subtype:<10}"
              f"0x{partition.offset:08x}  0x{partition.size:08x}  0x{partition.end:08x}")
    used = sum(p.size for p in partitions)
    print(f"\nallocated {used} of {flash_size} bytes "
          f"({used / flash_size:.1%}), {flash_size - used} unallocated")
    slot = next((p for p in partitions if p.name == "ota_0"), None)
    if slot and app_size is not None:
        free = slot.size - app_size
        print(f"application {app_size} bytes in a 0x{slot.size:x} slot: "
              f"{free} bytes free ({free / slot.size:.1%})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--app-size", type=Path,
                        help="built application binary, to enforce the free-space gate")
    parser.add_argument("--flash-size", default=None,
                        help="flash size the table must fit, e.g. 8MB or 16MB "
                             f"(default {FLASH_SIZE // (1024 * 1024)}MB, the product board)")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    flash_size = FLASH_SIZE
    if args.flash_size is not None:
        try:
            flash_size = parse_size(args.flash_size)
        except ValueError as error:
            print(f"ERROR: --flash-size: {error}", file=sys.stderr)
            return 1

    try:
        partitions = load(args.csv)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    app_size = None
    if args.app_size:
        if not args.app_size.is_file():
            print(f"ERROR: {args.app_size} does not exist", file=sys.stderr)
            return 1
        app_size = args.app_size.stat().st_size

    problems = check(partitions, app_size, flash_size)
    if not args.quiet:
        report(partitions, app_size, flash_size)
    for problem in problems:
        print(f"ERROR: {problem}", file=sys.stderr)
    print(f"\ncheck_partitions: {len(partitions)} partitions, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
