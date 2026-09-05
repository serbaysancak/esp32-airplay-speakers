#!/usr/bin/env python3
"""Tests for check_partitions.py.

The partition gate is the only thing standing between a hand-edited offset and
a board that does not boot, so it needs its own tests: a validator that silently
accepts everything looks exactly like a validator that passes.

Each case mutates one field of the real firmware/partitions.csv and asserts that
the tool notices. The good case asserts the unmodified table still passes, so a
rule that is too strict fails here too.

    python3 firmware/tools/test_check_partitions.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_partitions import FLASH_SIZE, check, load, parse_size  # noqa: E402

REAL_CSV = Path(__file__).resolve().parent.parent / "partitions.csv"
DEVKIT_CSV = Path(__file__).resolve().parent.parent / "partitions-devkit.csv"


def mutate(original: str, replacement: str) -> list[str]:
    """Rewrite one row of the real table and return the resulting problems."""
    text = REAL_CSV.read_text(encoding="utf-8")
    if original not in text:
        raise AssertionError(f"fixture row not found, the table changed shape: {original!r}")
    path = Path("/tmp/hk_partition_fixture.csv")
    path.write_text(text.replace(original, replacement), encoding="utf-8")
    return check(load(path), None)


CASES: list[tuple[str, str, str, str]] = [
    ("mismatched slot sizes",
     "ota_1,          app,  ota_1,    0x700000,  0x6e0000,",
     "ota_1,          app,  ota_1,    0x700000,  0x600000,",
     "equally sized slots"),
    ("second slot with the wrong subtype",
     "ota_1,          app,  ota_1,    0x700000,  0x6e0000,",
     "ota_1,          app,  ota_0,    0x700000,  0x6e0000,",
     "type and subtype"),
    ("slot demoted to a data partition",
     "ota_1,          app,  ota_1,    0x700000,  0x6e0000,",
     "ota_1,          data, spiffs,   0x700000,  0x6e0000,",
     "type and subtype"),
    ("app partition off the 64 KB grid",
     "ota_0,          app,  ota_0,    0x20000,   0x6e0000,",
     "ota_0,          app,  ota_0,    0x21000,   0x6e0000,",
     "aligned"),
    ("partition growing over the partition table",
     "nvs,            data, nvs,      0x9000,    0x6000,",
     "nvs,            data, nvs,      0x8000,    0x7000,",
     "bootloader or the partition table"),
    ("table running past the end of flash",
     "storage,        data, spiffs,   0xde0000,  0x220000,",
     "storage,        data, spiffs,   0xde0000,  0x320000,",
     "past the"),
    ("otadata resized",
     "otadata,        data, ota,      0xf000,    0x2000,",
     "otadata,        data, ota,      0xf000,    0x4000,",
     "exactly 0x2000"),
    # The long form is deliberate: ESP-IDF truncates a label to 16 characters,
    # which is why the partition is called factory_cal and not something longer.
    ("calibration partition renamed beyond the label limit",
     "factory_cal,    data, nvs,      0x13000,   0xd000,",
     "factory_cal" + "ibration, data, nvs, 0x13000,   0xd000,",
     "characters"),
    ("calibration partition removed",
     "factory_cal,    data, nvs,      0x13000,   0xd000,",
     "",
     "missing partition"),
]


def main() -> int:
    failures = 0

    problems = check(load(REAL_CSV), None)
    if problems:
        print(f"FAIL the real partitions.csv should pass, but reported: {problems}")
        failures += 1
    else:
        print("ok   the real partitions.csv passes")

    # The second board. Two tables and one flash-size constant is how a devkit
    # table silently gets validated against 16 MB of flash it does not have, so
    # both directions of the pairing are asserted here.
    problems = check(load(DEVKIT_CSV), None, 8 * 1024 * 1024)
    if problems:
        print(f"FAIL partitions-devkit.csv should pass at 8 MB, but reported: {problems}")
        failures += 1
    else:
        print("ok   partitions-devkit.csv passes at 8 MB")

    problems = check(load(REAL_CSV), None, 8 * 1024 * 1024)
    if any("past the 8 MB flash" in problem for problem in problems):
        print("ok   caught: the 16 MB product table does not fit 8 MB of flash")
    else:
        print(f"FAIL missed: the product table checked against 8 MB, got {problems}")
        failures += 1

    problems = check(load(DEVKIT_CSV), None)
    if problems:
        print(f"FAIL the devkit table should still fit the default 16 MB: {problems}")
        failures += 1
    else:
        print("ok   the devkit table fits the default flash size too")

    # A bare number means bytes. Reading "8" as 8 MB would turn a typo into a
    # flash size every table passes against.
    size_cases = [("8MB", 8 * 1024 * 1024), ("8M", 8 * 1024 * 1024),
                  ("0x800000", 8 * 1024 * 1024), ("8388608", 8 * 1024 * 1024),
                  ("16MB", FLASH_SIZE), ("8", 8)]
    for text, expected_size in size_cases:
        try:
            actual = parse_size(text)
        except ValueError as error:
            print(f"FAIL parse_size({text!r}) raised {error}")
            failures += 1
            continue
        if actual == expected_size:
            print(f"ok   parse_size({text!r}) = {actual}")
        else:
            print(f"FAIL parse_size({text!r}) = {actual}, expected {expected_size}")
            failures += 1

    for text in ("banana", "0", "-4MB"):
        try:
            parse_size(text)
        except ValueError:
            print(f"ok   parse_size rejects {text!r}")
        else:
            print(f"FAIL parse_size accepted {text!r}")
            failures += 1

    for name, original, replacement, expected in CASES:
        problems = mutate(original, replacement)
        if any(expected in problem for problem in problems):
            print(f"ok   caught: {name}")
        else:
            print(f"FAIL missed: {name}\n     expected a problem containing {expected!r}, got {problems}")
            failures += 1

    print(f"\n{len(CASES) + 1 + 3 + len(size_cases) + 3} cases, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
