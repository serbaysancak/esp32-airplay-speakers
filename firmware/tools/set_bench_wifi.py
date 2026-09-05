#!/usr/bin/env python3
"""Put the bench devkit on a Wi-Fi network, without the password passing through
anyone else's hands.

    python3 firmware/tools/set_bench_wifi.py

Writes firmware/sdkconfig.devkit.local, which git ignores. The next devkit build
carries the credentials and the board joins on boot.

Why this exists at all, since the product has two proper provisioning paths: the
devkit has no button, so the press that clears credentials cannot be given, and
joining its SoftAP from the machine driving the board takes that machine off the
network it is being told to join. Both are properties of the bench, not of the
firmware, so the workaround is confined to the bench.

The cost is real and worth stating plainly: the password ends up inside the
built image. That is why this is limited to a board with nothing attached, and
why the file it writes is never committed. Run with --clear when finished.
"""

from __future__ import annotations

import argparse
import getpass
import os
import subprocess
import sys
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
LOCAL = FIRMWARE / "sdkconfig.devkit.local"

#: ESP-IDF's own limits. Truncation here would produce a board that fails to
#: join for a reason nothing on the bench would explain.
SSID_MAX = 32
PASSWORD_MAX = 63


def quote(value: str) -> str:
    """Kconfig string literal. Backslash first, or the escaping escapes itself."""
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def refuse_if_tracked() -> None:
    """A tracked sdkconfig.devkit.local would put a password in the history.

    .gitignore does not apply to a file git already tracks, so the ignore rule
    alone is not the guarantee it looks like.
    """
    result = subprocess.run(["git", "ls-files", "--error-unmatch", str(LOCAL)],
                            cwd=FIRMWARE, capture_output=True, text=True)
    if result.returncode == 0:
        raise SystemExit(
            f"refusing to write: {LOCAL.name} is tracked by git, so its contents "
            f"would be committed. Run `git rm --cached {LOCAL}` first.")


def write(ssid: str, password: str) -> None:
    LOCAL.write_text(
        "# Bench Wi-Fi for the N8R2 devkit. NEVER COMMIT THIS FILE.\n"
        "# Written by firmware/tools/set_bench_wifi.py; remove it with --clear.\n"
        f"CONFIG_HK_DEVKIT_WIFI_SSID={quote(ssid)}\n"
        f"CONFIG_HK_DEVKIT_WIFI_PASSWORD={quote(password)}\n",
        encoding="utf-8")
    os.chmod(LOCAL, 0o600)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clear", action="store_true",
                        help="write empty credentials, so the next build carries none")
    args = parser.parse_args(argv)

    refuse_if_tracked()

    if args.clear:
        write("", "")
        print(f"cleared {LOCAL}")
        print("Rebuild and reflash for the board to stop carrying them.")
        return 0

    ssid = input("Wi-Fi SSID: ").strip()
    if not ssid:
        print("error: no SSID given", file=sys.stderr)
        return 1
    if len(ssid.encode("utf-8")) > SSID_MAX:
        print(f"error: SSID is longer than {SSID_MAX} bytes", file=sys.stderr)
        return 1

    # getpass, so it is not echoed and does not reach the shell history.
    password = getpass.getpass("Wi-Fi password (hidden): ")
    if len(password.encode("utf-8")) > PASSWORD_MAX:
        print(f"error: password is longer than {PASSWORD_MAX} bytes", file=sys.stderr)
        return 1

    write(ssid, password)
    # The SSID is echoed and the password is not, deliberately: seeing which
    # network was recorded is how a typo is caught before a flash.
    print(f"\nwrote {LOCAL} (mode 600) for SSID {ssid!r}")
    print("It is ignored by git. Build the devkit target and flash; the board joins on boot.")
    print("Run with --clear when you are done with this network.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
