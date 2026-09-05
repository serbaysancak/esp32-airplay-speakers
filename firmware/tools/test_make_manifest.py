#!/usr/bin/env python3
"""Tests for make_manifest.py.

The generator's job is to make manifest-versus-image disagreement impossible,
so the cases that matter are the ones where something tries to introduce a
disagreement anyway.
"""
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_manifest  # noqa: E402

TOOL = Path(__file__).resolve().parent / "make_manifest.py"


def make_image(version="0.2.0", project="harman-kardom", secure_version=0,
               chip_id=0x0009, magic=0xABCD5432, first_byte=0xE9, payload=4096):
    """Assemble a minimal but structurally correct ESP32-S3 app image."""
    header = bytearray(24)
    header[0] = first_byte
    struct.pack_into("<H", header, 12, chip_id)

    segment = bytearray(8)

    desc = bytearray(256)
    struct.pack_into("<II", desc, 0, magic, secure_version)
    desc[16:16 + len(version)] = version.encode()
    desc[48:48 + len(project)] = project.encode()

    body = bytes(payload)
    return bytes(header) + bytes(segment) + bytes(desc) + body


class TestReadAppDesc(unittest.TestCase):
    def _write(self, blob):
        path = Path(self.tmp.name) / "image.bin"
        path.write_bytes(blob)
        return path

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_reads_the_fields_it_publishes(self):
        path = self._write(make_image(version="1.4.2", project="harman-kardom",
                                      secure_version=3))
        desc = make_manifest.read_app_desc(path)
        self.assertEqual(desc["version"], "1.4.2")
        self.assertEqual(desc["project_name"], "harman-kardom")
        self.assertEqual(desc["secure_version"], 3)

    def test_rejects_a_non_image(self):
        path = self._write(b"this is not firmware")
        with self.assertRaises(make_manifest.ImageError):
            make_manifest.read_app_desc(path)

    def test_rejects_a_missing_image_magic(self):
        path = self._write(make_image(first_byte=0x00))
        with self.assertRaises(make_manifest.ImageError):
            make_manifest.read_app_desc(path)

    def test_rejects_another_chip(self):
        """An ESP32-C3 build must not be published as an S3 release."""
        path = self._write(make_image(chip_id=0x0005))
        with self.assertRaises(make_manifest.ImageError) as caught:
            make_manifest.read_app_desc(path)
        self.assertIn("ESP32-S3", str(caught.exception))

    def test_rejects_a_bad_descriptor_magic(self):
        """Reading at the wrong offset produces plausible garbage, so the magic
        word is the only thing that catches it."""
        path = self._write(make_image(magic=0x12345678))
        with self.assertRaises(make_manifest.ImageError):
            make_manifest.read_app_desc(path)


class TestBuildManifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _run(self, image_blob, tag="v0.2.0", extra=()):
        path = Path(self.tmp.name) / "image.bin"
        path.write_bytes(image_blob)
        out = Path(self.tmp.name) / "manifest.json"
        result = subprocess.run(
            [sys.executable, str(TOOL), "--image", str(path), "--tag", tag,
             "--asset-url", "https://github.com/o/r/releases/download/x/hk.bin",
             "--out", str(out), *extra],
            capture_output=True, text=True)
        return result, out

    def test_publishes_what_the_image_says(self):
        blob = make_image(version="0.2.0")
        result, out = self._run(blob)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(out.read_text())
        self.assertEqual(manifest["version"], "0.2.0")
        self.assertEqual(manifest["product"], "harman-kardom")
        self.assertEqual(manifest["target"], "esp32s3")
        self.assertEqual(manifest["size"], len(blob))
        self.assertEqual(manifest["sha256"], hashlib.sha256(blob).hexdigest())

    def test_tag_must_match_the_built_version(self):
        """The failure this exists for: tagging v0.3.0 and publishing an
        unchanged 0.2.0 binary, which the device would then refuse forever."""
        result, _ = self._run(make_image(version="0.2.0"), tag="v0.3.0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("0.3.0", result.stderr)
        self.assertIn("0.2.0", result.stderr)

    def test_tag_without_the_v_prefix_is_accepted(self):
        result, _ = self._run(make_image(version="0.2.0"), tag="0.2.0")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_image_larger_than_the_slot_is_refused(self):
        result, _ = self._run(make_image(payload=8192),
                              extra=("--slot-size", "4096"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("slot", result.stderr)

    def test_manifest_carries_every_field_the_device_requires(self):
        """hk_manifest refuses any manifest missing a required field, so the
        generator and the firmware have to agree on the list."""
        result, out = self._run(make_image(version="0.2.0"))
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(out.read_text())
        required = {"product", "version", "channel", "target", "hw_revision",
                    "asset", "size", "sha256", "secure_version",
                    "min_updater_version"}
        self.assertEqual(required - set(manifest), set())

    def test_sha256_is_lowercase_hex_of_the_right_length(self):
        _, out = self._run(make_image())
        digest = json.loads(out.read_text())["sha256"]
        self.assertEqual(len(digest), 64)
        self.assertEqual(digest, digest.lower())
        self.assertTrue(all(c in "0123456789abcdef" for c in digest))


class TestBoardSlotSize(unittest.TestCase):
    """The slot an image must fit belongs to the board, not to a default.

    There are two boards now: the N16R8 product board and the N8R2 bring-up
    devkit, whose OTA slot is less than half the size. A single hard-coded
    default meant a devkit image was measured against a slot 76% larger than the
    flash it was going to, so the one gate standing between an oversized image
    and a bricked update would have passed it.
    """

    def test_every_board_the_firmware_knows_has_a_slot_size(self):
        # hk_identity.h and this table must name the same boards. A board that
        # exists in the firmware but not here cannot be published at all.
        self.assertIn("prototype-n16r8", make_manifest.BOARD_SLOT_SIZE)
        self.assertIn("devkit-n8r2", make_manifest.BOARD_SLOT_SIZE)

    def test_each_slot_size_matches_that_board_s_partition_table(self):
        firmware = Path(__file__).resolve().parent.parent
        tables = {
            "prototype-n16r8": firmware / "partitions.csv",
            "devkit-n8r2": firmware / "partitions-devkit.csv",
        }
        for revision, table in tables.items():
            with self.subTest(revision=revision):
                row = [line for line in table.read_text(encoding="utf-8").splitlines()
                       if line.startswith("ota_0,")]
                self.assertEqual(len(row), 1, f"{table.name} has no single ota_0 row")
                size = int(row[0].split(",")[4].strip(), 0)
                self.assertEqual(make_manifest.BOARD_SLOT_SIZE[revision], size,
                                 f"{table.name} and BOARD_SLOT_SIZE disagree")

    def test_an_unknown_board_is_refused_rather_than_given_a_default(self):
        code = make_manifest.main([
            "--image", str(self.image), "--tag", "v0.1.0",
            "--asset-url", "https://example.invalid/a.bin",
            "--hw-revision", "some-other-board",
        ])
        self.assertEqual(code, 1)

    def test_a_slot_size_that_contradicts_the_board_is_refused(self):
        code = make_manifest.main([
            "--image", str(self.image), "--tag", "v0.1.0",
            "--asset-url", "https://example.invalid/a.bin",
            "--hw-revision", "devkit-n8r2", "--slot-size", "0x6e0000",
        ])
        self.assertEqual(code, 1)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.image = Path(self.tmp.name) / "app.bin"
        self.image.write_bytes(make_image(version="0.1.0"))


class TestGeneratorAgreesWithFirmware(unittest.TestCase):
    """The two ends of the contract, checked against each other.

    hk_manifest_json.c names every field it will read; hk_manifest.h says all of
    them are required and a missing one is refused. If this generator spells
    one differently — hw_rev for hw_revision, say — nothing fails at build
    time, nothing fails in CI, and every device refuses every update forever
    with a message about a missing field. Comparing the two lists is the only
    place that mismatch is cheap to catch.
    """

    def setUp(self):
        self.firmware = Path(__file__).resolve().parents[1]
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _generated_keys(self):
        path = Path(self.tmp.name) / "image.bin"
        path.write_bytes(make_image(version="0.2.0"))
        out = Path(self.tmp.name) / "m.json"
        result = subprocess.run(
            [sys.executable, str(TOOL), "--image", str(path), "--tag", "v0.2.0",
             "--asset-url", "https://github.com/o/r/releases/download/x/hk.bin",
             "--out", str(out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return set(json.loads(out.read_text()))

    def _keys_the_client_reads(self):
        source = (self.firmware / "components/hk_ota/hk_manifest_json.c").read_text()
        keys = set(re.findall(r'take_(?:string|u32)\(root, "([a-z0-9_]+)"', source))
        self.assertTrue(keys, "found no manifest keys in hk_manifest_json.c")
        return keys

    def test_every_field_the_client_reads_is_generated(self):
        missing = self._keys_the_client_reads() - self._generated_keys()
        self.assertEqual(missing, set(),
                         f"firmware reads fields the generator never writes: {missing}")

    def test_the_generator_writes_nothing_the_client_ignores(self):
        extra = self._generated_keys() - self._keys_the_client_reads()
        self.assertEqual(extra, set(),
                         f"generator writes fields the firmware never reads: {extra}")

    def test_the_hardware_revision_matches_the_firmware(self):
        """The device refuses a manifest whose hw_revision is not its own, so
        two spellings produce a speaker that rejects every release with a
        message about hardware — which reads like a hardware fault."""
        header = (self.firmware / "components/hk_identity/include/hk_identity.h").read_text()
        match = re.search(r'#define\s+HK_HW_REVISION\s+"([^"]+)"', header)
        self.assertIsNotNone(match, "HK_HW_REVISION not found in hk_identity.h")
        firmware_revision = match.group(1)

        tool = TOOL.read_text()
        default = re.search(
            r'--hw-revision"[^)]*?default="([^"]+)"', tool, re.S)
        self.assertIsNotNone(default, "no --hw-revision default in make_manifest.py")
        self.assertEqual(default.group(1), firmware_revision)

    def test_the_field_list_matches_hk_manifest_required(self):
        """And both match the bitmask the validator insists on."""
        header = (self.firmware / "components/hk_manifest/include/hk_manifest.h").read_text()
        block = header.split("HK_MANIFEST_REQUIRED_FIELDS")[1].split("/**")[0]
        bits = set(re.findall(r"HK_MF_([A-Z0-9_]+)", block))
        expected = {
            "PRODUCT": "product", "VERSION": "version", "CHANNEL": "channel",
            "TARGET": "target", "HW_REVISION": "hw_revision", "ASSET": "asset",
            "SIZE": "size", "SHA256": "sha256", "SECURE_VER": "secure_version",
            "MIN_UPDATER": "min_updater_version",
        }
        self.assertEqual(bits, set(expected),
                         "HK_MANIFEST_REQUIRED_FIELDS changed; update the generator")
        self.assertEqual(set(expected.values()), self._generated_keys())


#: Where the end-to-end harness lives. Overridable because CI builds it in a
#: different directory, and because a symlink shuffle to make one path look
#: like another is the kind of trick that works until the directory it is
#: pretending to be already exists.
HARNESS = Path(os.environ.get(
    "HK_MANIFEST_E2E",
    str(Path(__file__).resolve().parents[2] / "build/host-tests/manifest_e2e")))


@unittest.skipUnless(HARNESS.exists(),
                     f"{HARNESS} not built; run cmake --build build/host-tests")
class TestEndToEnd(unittest.TestCase):
    """A generated manifest, through the firmware's own parser and validator.

    Every other test compares the two ends by field NAME. This one compares
    them by VALUE, which is the half that was never checked: a version the
    parser spells differently, a digest in the wrong case, a size that
    overflows, a URL longer than the buffer — each of those would have passed
    the whole suite and been discovered by four speakers refusing every
    release.
    """

    DEVICE = ("harman-kardom", "esp32s3", "prototype-n16r8", "stable")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _manifest(self, version="0.2.0", url=None, extra=()):
        image = Path(self.tmp.name) / "image.bin"
        image.write_bytes(make_image(version=version))
        out = Path(self.tmp.name) / "manifest.json"
        url = url or ("https://github.com/ktahatastan/esp32-airplay-speakers"
                      f"/releases/download/v{version}/harman-kardom.bin")
        result = subprocess.run(
            [sys.executable, str(TOOL), "--image", str(image), "--tag", f"v{version}",
             "--asset-url", url, "--out", str(out), *extra],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return out

    def _judge(self, manifest, running="0.1.0", secure="0", slot="0x6e0000",
               device=None):
        product, target, hw, channel = device or self.DEVICE
        return subprocess.run(
            [str(HARNESS), str(manifest), product, target, hw, channel,
             running, secure, slot],
            capture_output=True, text=True)

    def test_a_generated_manifest_is_accepted(self):
        """The case that must work, or nothing else matters."""
        result = self._judge(self._manifest())
        self.assertIn("\nok\n", result.stdout, result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_every_required_field_survives_the_round_trip(self):
        """0x3ff is all ten required bits. A field the generator writes under a
        name the parser does not read would show up here as a missing bit."""
        result = self._judge(self._manifest())
        self.assertIn("present=0x3ff", result.stdout, result.stdout)

    def test_the_real_asset_url_fits_the_firmware_buffer(self):
        """HK_MANIFEST_ASSET_MAX was 96 once, and this repository's own URL is
        97. The parser drops a field it cannot hold, so a URL that is too long
        arrives as a missing asset rather than as a truncated one."""
        result = self._judge(self._manifest())
        self.assertIn("asset_len=", result.stdout)
        length = int(result.stdout.split("asset_len=")[1].split("\n")[0])
        self.assertGreater(length, 80)
        self.assertIn("present=0x3ff", result.stdout)

    def test_a_very_long_url_is_refused_not_truncated(self):
        long_url = ("https://github.com/ktahatastan/esp32-airplay-speakers"
                    "/releases/download/v0.2.0/" + "a" * 300 + ".bin")
        result = self._judge(self._manifest(url=long_url))
        self.assertIn("field_missing", result.stdout, result.stdout)
        self.assertNotEqual(result.returncode, 0)

    def test_another_product_is_refused(self):
        result = self._judge(self._manifest(),
                             device=("something-else", "esp32s3",
                                     "prototype-n16r8", "stable"))
        self.assertIn("wrong_product", result.stdout, result.stdout)

    def test_other_hardware_is_refused(self):
        result = self._judge(self._manifest(),
                             device=("harman-kardom", "esp32s3",
                                     "rev-b", "stable"))
        self.assertIn("wrong_hardware", result.stdout, result.stdout)

    def test_a_release_that_is_not_newer_is_refused(self):
        result = self._judge(self._manifest(version="0.2.0"), running="0.2.0")
        self.assertIn("not_newer", result.stdout, result.stdout)

    def test_an_image_larger_than_the_slot_is_refused(self):
        result = self._judge(self._manifest(), slot="4096")
        self.assertIn("bad_size", result.stdout, result.stdout)

    def test_a_channel_the_device_does_not_follow_is_refused(self):
        manifest = self._manifest(extra=("--channel", "beta"))
        result = self._judge(manifest)
        self.assertIn("wrong_channel", result.stdout, result.stdout)


if __name__ == "__main__":
    unittest.main()
