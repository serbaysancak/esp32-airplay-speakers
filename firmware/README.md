# Harman Kardom firmware

ESP32-S3 firmware for one speaker. Four speakers run the same image and are told
apart by a device identity derived from their MAC.

**Stage F0.** This build comes up, reports what it is, and proves the skeleton
links. It plays no audio, joins no network and drives no GPIO. What comes next,
in order and with acceptance criteria, is in
[docs/03-firmware/firmware-plan.md](../docs/03-firmware/firmware-plan.md).

## Locked inputs

| Input | Value | Source |
|---|---|---|
| Board | ESP32-S3, 16 MB flash + 8 MB octal PSRAM (`N16R8`) | ADR-0010 |
| ESP-IDF | `v5.5.1`, pinned | this file and `.github/workflows/firmware-ci.yml` |
| Audio topology | mono program, bi-amp: left path woofer, right path tweeter | ADR-0002 |
| Distribution | SemVer tag, GitHub Releases, signed A/B OTA | ADR-0008 |
| AirPlay stack | **not chosen** | ADR-0007 is open |

The GPIO assignment is a *candidate*, not accepted: it holds until the purchased
board's own schematic and a boot test confirm it.

## Build

```bash
git clone --branch v5.5.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32s3
. ~/esp/esp-idf/export.sh
idf.py -C firmware build
```

Flash and watch the boot report:

```bash
idf.py -C firmware -p /dev/tty.usbmodem* flash monitor
```

> Changing `sdkconfig.defaults` does **not** update an existing `sdkconfig`.
> ESP-IDF applies the defaults only when it generates the file, so a local build
> keeps the old settings and silently omits whatever you just enabled. Delete
> `firmware/sdkconfig` and rebuild after editing the defaults. CI is immune,
> since it starts from a fresh checkout.

`PROJECT_VER` comes from `version.txt`. It must stay strict SemVer, because the
OTA client compares it numerically and the release pipeline checks it against the
Git tag.

### The bring-up devkit

The product board is not the only target. While it is unavailable, the firmware
also builds for an ESP32-S3 **N8R2** development kit — 8 MB flash, 2 MB quad
PSRAM — so the network and control paths can be exercised on real silicon
(ADR-0012). It is a bring-up target and cannot ship.

```bash
idf.py -C firmware -B firmware/build-devkit \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.devkit" \
  -D SDKCONFIG="$PWD/firmware/build-devkit/sdkconfig" \
  build
```

The separate `-B` and `-D SDKCONFIG` are not tidiness. The two profiles disagree
about flash size, PSRAM mode and the partition table, and ESP-IDF applies
defaults only when it first writes an `sdkconfig` — sharing one would silently
keep whichever profile was configured first, and that shows up as a board that
does not boot rather than as a build error.

Four settings define a board: flash size, PSRAM mode, partition table and OTA
hardware revision. `CMakeLists.txt` refuses to configure a build in which they
disagree, and refuses to sign a devkit build at all. On the board itself the boot
report opens with the revision it was built for:

```text
hk: board       devkit-n8r2
hk: flash       8 MB detected
hk: psram       2 MB
```

## Verify

Three checks run without any hardware, and all three run in CI.

```bash
# 1. Host unit tests: pure logic, no ESP-IDF needed
cmake -S firmware/test -B build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure

# 2. Partition layout, and the image size gate once a build exists
python3 firmware/tools/check_partitions.py
python3 firmware/tools/check_partitions.py --app-size firmware/build/harman-kardom.bin

# 3. The partition validator's own tests, so a gate that accepts everything
#    cannot pass unnoticed
python3 firmware/tools/test_check_partitions.py

# 4. A user reset must not be able to erase the calibration store (PRD-008)
python3 firmware/tools/check_storage_isolation.py

# 5. No log statement may print a credential
python3 firmware/tools/check_no_credential_logs.py

# 6. Documentation integrity
python3 scripts/check_docs.py
```

### What has been verified, and what has not

Verified on 2026-08-31 with ESP-IDF v5.5.1 on macOS: the project builds clean
with no warnings in project sources, the host suite passes, the partition gate
and its own tests pass, and `PROJECT_VER` in the built image matches
`version.txt`.

The button and LED now have a driver: the pin is read, the PWM runs, and the
network layer starts Wi-Fi, mDNS and SoftAP provisioning. None of that has run
on a board, so treat every behaviour below the policy modules as written but
unproven.

Provisioning will not open on a device whose per-device credentials have not
been written, and that is on purpose: the firmware refuses rather than falling
back to a weaker security mode. Generate them with

```bash
. $IDF_PATH/export.sh
python3 firmware/tools/provision_credentials.py --device A1B2
```

Each device gets its own random password. The speaker stores only an SRP6a salt
and verifier, from which the password cannot be recovered, so reading the flash
off a speaker does not yield the credential. The generated `label.txt` is the
only copy of the password; it is written owner-only and must not be committed. The transport is chosen by the situation, not by the caller: SoftAP with
nothing stored, BLE from a button press on a configured device. ADR-0005
option C, because ESP-IDF cannot run both in one session.

**Not verified: nothing has run on hardware.** No board has been flashed, so the
boot report, the GPIO assignment and the PSRAM detection are unexercised. The
pin table stays a candidate until that happens.

## Layout

```text
firmware/
  CMakeLists.txt        project definition; PROJECT_VER comes from version.txt
  sdkconfig.defaults    board, partition, PSRAM and rollback settings
  sdkconfig.devkit      overlay for the N8R2 bring-up board (ADR-0012)
  partitions.csv        16 MB layout: dual OTA slots + isolated calibration
  partitions-devkit.csv the same rows at 8 MB, identical below 0x20000
  version.txt           strict SemVer, compared by the OTA client
  main/                 app_main: boot report only at F0
  components/
    hk_pins/            GPIO assignment; the compiler enforces the constraints
    hk_identity/        every user-visible name, derived from the MAC
    hk_version/         SemVer parsing and the OTA update decision
    hk_button/          function button: debounce, hold levels, what commits
    hk_led/             which status wins the single LED, and how it looks
    hk_provision/       when the setup radios are open, and when they shut
    hk_ui/              button GPIO and RGB PWM, on its own low-priority task
    hk_network/         Wi-Fi, mDNS and the provisioning transport
    hk_schema/          what to do when stored data does not match this build
    hk_storage/         the two stores, and the wall between them
  test/                 host unit tests, built with plain CMake
  tools/                partition and size validation
```

Components with no ESP-IDF dependency are deliberately pure C. That is what
makes them testable on a laptop, years before a driver is safe to energise.

## What the compiler enforces

`components/hk_pins/include/hk_pins.h` is the single source of truth for GPIO
assignment, and it is not merely documentation:

- no two functions may share a pin
- no pin may land on a strapping pin (`GPIO0/3/45/46`) or on the native USB pair
  (`GPIO19/20`), which the documented USB/UART recovery path needs
- no pin may exceed the highest ESP32-S3 GPIO

Each is a `_Static_assert`, so a violation fails the build with a readable
message instead of producing a board that boots into the wrong mode. The host
test additionally checks the table against the pin table published in
[the wiring plan](../docs/02-hardware/circuit-and-wiring-plan.md), function by
function, so a documentation change and a code change cannot drift apart
silently.

## Safety

This firmware will eventually drive an amplifier connected to drivers whose
impedance has not been measured. Until the relevant gate passes:

- no code path may raise output level on a real driver (G0, G2)
- the tweeter path stays muted without a verified high-pass and limiter
- a user reset must never erase `factory_cal`: enforced by a partition boundary,
  a read-only open, and `tools/check_storage_isolation.py` in CI
- OTA must not start on low battery, high temperature or during playback

An automated test can show that logic behaves. It cannot show that a gate
passed; only a recorded operator measurement can.
