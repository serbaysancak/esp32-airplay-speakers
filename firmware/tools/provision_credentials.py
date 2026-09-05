#!/usr/bin/env python3
"""Generate the per-device provisioning credentials and the label that carries them.

Every Harman Kardom speaker gets its own provisioning password. There is no
shared factory password, because four speakers on one network with one password
means compromising one compromises all of them, and a password printed in a
public repository is not a password at all.

The device never stores the password. Security 2 is SRP6a: the device holds a
salt and a verifier, from which the password cannot be recovered, and proves
knowledge of it without either side transmitting it. Reading the flash off a
speaker therefore does not yield the credential. What this tool produces:

  <out>/<id>/factory_cal.csv   NVS CSV, input to nvs_partition_gen.py
  <out>/<id>/prov_salt.bin     the salt, referenced by the CSV
  <out>/<id>/prov_verif.bin    the verifier, referenced by the CSV
  <out>/<id>/label.txt         the password and QR payload, for the printed label
  <out>/<id>/qr.txt            just the QR payload

The label file is the only place the password exists. It is written with
owner-only permissions and must not be committed; keep it wherever the rest of
the household's secrets live.

    python3 firmware/tools/provision_credentials.py --count 4
    python3 firmware/tools/provision_credentials.py --device A1B2

SRP6a comes from ESP-IDF's own tools/esp_prov/security/srp6a.py. Using the
vendor implementation rather than a second one guarantees the tool and the
device agree; a reimplementation that differed by one hash would fail only at
provisioning time, on a device already in a box.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import secrets
import string
import subprocess
import sys
from pathlib import Path

#: Shared by the tool, the QR payload and the verifier computation. Not a
#: secret: it identifies the scheme, and the password is what protects it.
USERNAME = "harmankardom"

#: Product surfaces, from docs/controls-and-provisioning-plan.md and ADR-0001.
SOFTAP_NAME = "HarmanKardom-Setup-{device_id}"
BLE_NAME = "HarmanKardom-{device_id}"

#: Salt length requested from the generator, in bytes.
#:
#: What comes back may be one byte shorter. The generator derives the salt from
#: a random integer and serialises it minimally, so a value with a zero top byte
#: loses it — measured at roughly 1 in 256, for both the salt and the verifier.
#: That is fine and must not be padded: the verifier is computed over the salt
#: as a raw byte string, so padding would break the handshake. The firmware
#: accepts a range and uses whatever length was stored.
SALT_LEN = 16

#: Password length. The alphabet below has 30 symbols, so 12 characters carry
#: about 59 bits: far beyond anything a rate-limited provisioning session can be
#: brute forced through, while still being typable off a printed label.
PASSWORD_LEN = 12

#: Ambiguous glyphs are left out. This gets read off a small label and typed
#: into a phone, and 0/O or 1/l/I costs a support call every time.
ALPHABET = "".join(c for c in (string.ascii_uppercase + string.digits)
                   if c not in "O0I1LS5B8")


def load_srp6a():
    """Import ESP-IDF's SRP6a implementation."""
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise SystemExit(
            "IDF_PATH is not set. Run . $IDF_PATH/export.sh first; this tool uses "
            "ESP-IDF's own SRP6a implementation rather than a second one.")
    esp_prov = Path(idf_path) / "tools" / "esp_prov"
    module_path = esp_prov / "security" / "srp6a.py"
    if not module_path.is_file():
        raise SystemExit(f"{module_path} not found; is IDF_PATH correct?")

    # esp_prov goes on the path because srp6a imports its sibling utils module.
    if str(esp_prov) not in sys.path:
        sys.path.insert(0, str(esp_prov))

    # The module is then loaded straight from its file rather than as
    # security.srp6a: importing that package runs security/__init__.py, which
    # pulls in esp_prov's protobuf stubs. This tool only needs to compute a
    # verifier, and should not need a protobuf install to print a label.
    spec = importlib.util.spec_from_file_location("hk_srp6a", module_path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"could not load {module_path}")
    srp6a = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(srp6a)
    except Exception as error:  # noqa: BLE001 - report whatever the import raised
        raise SystemExit(f"could not load ESP-IDF's srp6a module: {error}") from error
    return srp6a


def make_password() -> str:
    return "".join(secrets.choice(ALPHABET) for _ in range(PASSWORD_LEN))


def qr_payload(device_id: str, password: str, transport: str) -> str:
    """The payload the Espressif provisioning apps expect for Security 2.

    Field order and names follow ESP-IDF's own wifi_prov_print_qr().
    """
    name = BLE_NAME if transport == "ble" else SOFTAP_NAME
    return json.dumps({
        "ver": "v1",
        "name": name.format(device_id=device_id),
        "username": USERNAME,
        "pop": password,
        "transport": transport,
    }, separators=(",", ":"))


#: factory_cal size, from firmware/partitions.csv. Both boards use the same one.
IMAGE_SIZE = 0xD000
PARTITION_HINT = "firmware/partitions.csv"


class ImageError(Exception):
    """The partition image was not produced, or was produced wrong."""


def build_image(device_dir: Path) -> Path:
    """Generate the factory_cal image for one device, and prove it is usable.

    Run from inside the device directory on purpose. The CSV names prov_salt.bin
    and prov_verif.bin without a path, and nvs_partition_gen.py resolves those
    against the working directory rather than against the CSV -- so invoking it
    from the repository root silently fails to find the inputs. That is not a
    harmless mistake: the tool still writes an output file, and a short file
    flashed at the factory_cal offset erases the credentials that were there and
    puts nothing in their place, leaving a device that can never be provisioned.

    So the image is checked before anyone can flash it: exact size, and an NVS
    page header where an erased region would read 0xFF.
    """
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise ImageError("IDF_PATH is not set; source $IDF_PATH/export.sh first")
    generator = (Path(idf_path) / "components" / "nvs_flash"
                 / "nvs_partition_generator" / "nvs_partition_gen.py")
    if not generator.is_file():
        raise ImageError(f"{generator} does not exist")

    image = device_dir / "factory_cal.bin"

    def reject(reason: str) -> ImageError:
        """Remove the image before complaining about it.

        A failed run still leaves a partial file behind, and a partial file at
        this path is the whole danger: it looks like the thing to flash. Nothing
        that is not known-good survives this function.
        """
        image.unlink(missing_ok=True)
        return ImageError(reason)

    result = subprocess.run(
        [sys.executable, str(generator), "generate",
         "factory_cal.csv", "factory_cal.bin", hex(IMAGE_SIZE)],
        cwd=device_dir, capture_output=True, text=True)
    if result.returncode != 0:
        detail = (result.stderr.strip().splitlines() or ["no output"])[-1]
        raise reject(f"nvs_partition_gen.py failed: {detail}")
    if not image.is_file():
        raise reject("nvs_partition_gen.py reported success but wrote no image")

    size = image.stat().st_size
    if size != IMAGE_SIZE:
        raise reject(f"image is {size} bytes, not {IMAGE_SIZE}; a short image flashed "
                     f"at that offset erases the credentials and replaces nothing")
    if image.read_bytes()[:4] == b"\xff\xff\xff\xff":
        raise reject("image begins with erased flash, so it carries no NVS page: the "
                     "inputs named in factory_cal.csv were not found")
    return image


def write_device(out_dir: Path, device_id: str, srp6a) -> str:
    device_dir = out_dir / device_id
    device_dir.mkdir(parents=True, exist_ok=True)

    password = make_password()
    salt, verifier = srp6a.generate_salt_and_verifier(USERNAME, password, len_s=SALT_LEN)

    (device_dir / "prov_salt.bin").write_bytes(salt)
    (device_dir / "prov_verif.bin").write_bytes(verifier)

    # nvs_partition_gen.py CSV. The namespace and keys match hk_storage.h and
    # the reader in hk_network.c.
    (device_dir / "factory_cal.csv").write_text(
        "key,type,encoding,value\n"
        "cal,namespace,,\n"
        "schema,data,u32,1\n"
        "prov_salt,file,binary,prov_salt.bin\n"
        "prov_verif,file,binary,prov_verif.bin\n",
        encoding="utf-8")

    (device_dir / "qr.txt").write_text(qr_payload(device_id, password, "softap") + "\n",
                                       encoding="utf-8")

    label = device_dir / "label.txt"
    label.write_text(
        f"Harman Kardom {device_id}\n"
        f"\n"
        f"setup password : {password}\n"
        f"username       : {USERNAME}\n"
        f"softap ssid    : {SOFTAP_NAME.format(device_id=device_id)}\n"
        f"ble name       : {BLE_NAME.format(device_id=device_id)}\n"
        f"\n"
        f"QR (SoftAP, first setup):\n{qr_payload(device_id, password, 'softap')}\n"
        f"\n"
        f"QR (BLE, button-opened window):\n{qr_payload(device_id, password, 'ble')}\n"
        f"\n"
        f"This file is the only copy of the password. The speaker stores only a\n"
        f"salt and a verifier and cannot reveal it. Losing this file means the\n"
        f"device has to be re-flashed with new credentials.\n",
        encoding="utf-8")
    label.chmod(0o600)

    # Lengths are printed because they legitimately vary; a short one is not a
    # fault, and the firmware is written to accept it.
    print(f"{device_id}: salt {len(salt)} B, verifier {len(verifier)} B -> {device_dir}")
    return password


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path,
                        default=Path("build/provisioning"),
                        help="output directory (keep it out of Git)")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--device", action="append",
                       help="device id, the XXXX suffix from the label; repeatable")
    parser.add_argument("--image", action="store_true",
                        help="also build and verify each factory_cal.bin "
                             "(needs IDF_PATH)")
    group.add_argument("--count", type=int,
                       help="generate this many devices with placeholder ids")
    args = parser.parse_args()

    srp6a = load_srp6a()

    device_ids = args.device or [f"DEV{index + 1}" for index in range(args.count)]
    for device_id in device_ids:
        write_device(args.out, device_id, srp6a)

    print(f"\n{len(device_ids)} device(s) written under {args.out}")
    print("The label files hold the only copy of each password. Do not commit them.")

    if args.image:
        failures = 0
        for device_id in device_ids:
            try:
                image = build_image(args.out / device_id)
            except ImageError as error:
                print(f"ERROR: {device_id}: {error}", file=sys.stderr)
                failures += 1
                continue
            print(f"  {device_id}: {image} ({image.stat().st_size} bytes)")
        if failures:
            return 1
        print(f"\nFlash each at the factory_cal offset in {PARTITION_HINT}.")
        return 0

    print("\nBuild a partition image with --image, or by hand, per device:")
    print("  cd <out>/<id> && python3 \\")
    print("    $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \\")
    print(f"    generate factory_cal.csv factory_cal.bin 0x{IMAGE_SIZE:x}")
    print("The `cd` is not optional: the CSV names its input files without a path and")
    print("nvs_partition_gen.py resolves them against the working directory, not against")
    print("the CSV. Run it from anywhere else and the inputs are not found -- and a")
    print(f"truncated image flashed at that offset erases the credentials that were there.")
    print(f"Check the result is exactly {IMAGE_SIZE} bytes before flashing it.")
    print(f"Then flash it at the factory_cal offset from {PARTITION_HINT}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
