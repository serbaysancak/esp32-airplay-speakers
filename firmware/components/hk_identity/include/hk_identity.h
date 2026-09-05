/**
 * @file hk_identity.h
 * @brief Device naming for every user-visible surface.
 *
 * Implements the identity table in docs/controls-and-provisioning-plan.md and
 * ADR-0001. Four speakers on one network must be distinguishable at first boot,
 * so every name carries a short suffix derived from the device MAC.
 *
 * Pure C: no ESP-IDF dependency, so the naming rules can be tested on the host.
 */
#ifndef HK_IDENTITY_H
#define HK_IDENTITY_H

#include <stdint.h>

/** Product family name. The user may rename the AirPlay surface; this stays. */
#define HK_PRODUCT_FAMILY "Harman Kardom"

/**
 * Hardware revision. The product board is N16R8 (ADR-0010).
 *
 * The manifest carries the revision a release was built for and the device
 * refuses anything that does not match, so this string and the one
 * make_manifest.py writes have to agree exactly. Defined once here rather than
 * typed at each use: two spellings would produce a device that refuses every
 * release with a message about hardware, which reads like a hardware problem.
 *
 * The bring-up devkit overrides it from components/hk_identity/CMakeLists.txt,
 * because a devkit and a product board must never accept each other's releases:
 * they have different flash sizes and different partition tables, so an image
 * built for one does not merely misbehave on the other, it lands in the wrong
 * place. The override is driven by the Kconfig board option rather than a
 * command-line -D, so that selecting the board selects the revision with it.
 *
 * The default is the product value, which leaves the host tests and every build
 * that does not deliberately ask for the devkit unchanged.
 */
#ifndef HK_HW_REVISION
#define HK_HW_REVISION "prototype-n16r8"
#endif

/** Captive portal page title. */
#define HK_PORTAL_TITLE "Harman Kardom Kurulum"

/** Suffix length in characters, excluding the terminator. */
#define HK_SUFFIX_LEN 4

/*
 * Buffer sizes are the exact worst case: prefix + suffix + terminator. They are
 * checked against the protocol limits by static assertions below, so a future
 * rename cannot silently produce an SSID the radio will truncate.
 */
#define HK_NAME_AIRPLAY_SIZE (sizeof(HK_PRODUCT_FAMILY) + 1 + HK_SUFFIX_LEN)
#define HK_NAME_BLE_SIZE     (sizeof("HarmanKardom-") + HK_SUFFIX_LEN)
#define HK_NAME_SOFTAP_SIZE  (sizeof("HarmanKardom-Setup-") + HK_SUFFIX_LEN)
#define HK_NAME_MDNS_SIZE    (sizeof("harman-kardom-") + HK_SUFFIX_LEN)

/** IEEE 802.11 caps an SSID at 32 octets. */
_Static_assert(HK_NAME_SOFTAP_SIZE - 1 <= 32, "SoftAP SSID would be truncated");
/**
 * A BLE legacy advertising payload is 31 octets; the complete-local-name AD
 * structure costs 2 of them, so the name itself has 29 to work with.
 */
_Static_assert(HK_NAME_BLE_SIZE - 1 <= 29, "BLE local name would not fit one advertising packet");
/** A single DNS label is limited to 63 octets. */
_Static_assert(HK_NAME_MDNS_SIZE - 1 <= 63, "mDNS host label exceeds one DNS label");

/** Every name this device answers to. */
typedef struct {
    char suffix[HK_SUFFIX_LEN + 1];        /**< Uppercase, e.g. "A1B2" */
    char airplay[HK_NAME_AIRPLAY_SIZE];    /**< "Harman Kardom A1B2" */
    char ble[HK_NAME_BLE_SIZE];            /**< "HarmanKardom-A1B2" */
    char softap[HK_NAME_SOFTAP_SIZE];      /**< "HarmanKardom-Setup-A1B2" */
    char mdns[HK_NAME_MDNS_SIZE];          /**< "harman-kardom-a1b2", lowercase */
} hk_identity_t;

/** Result of ::hk_identity_from_mac. */
typedef enum {
    HK_IDENTITY_OK = 0,
    HK_IDENTITY_ERR_ARG = -1,      /**< NULL argument */
    HK_IDENTITY_ERR_MAC = -2,      /**< All-zero MAC: the interface is not initialised */
} hk_identity_err_t;

/**
 * Derive every surface name from a 6-octet MAC address.
 *
 * The suffix is the last two MAC octets in uppercase hex. Those are the
 * device-specific part of the address, so two units from the same batch differ
 * there. The mDNS name repeats it in lowercase because DNS labels are compared
 * case-insensitively and lowercase avoids surprising the user with mixed case.
 *
 * @param mac  six octets, as returned by esp_read_mac()
 * @param out  filled in on success, untouched otherwise
 * @return HK_IDENTITY_OK, or a negative ::hk_identity_err_t
 */
int hk_identity_from_mac(const uint8_t mac[6], hk_identity_t *out);

/**
 * Check that a name is a valid single DNS label: 1-63 characters of
 * [a-z0-9-], not starting or ending with a hyphen.
 *
 * @return 1 when valid, 0 otherwise.
 */
int hk_identity_is_valid_mdns_label(const char *label);

#endif /* HK_IDENTITY_H */
