/**
 * @file hk_network.h
 * @brief Wi-Fi, provisioning transport and mDNS.
 *
 * The hardware layer under hk_provision. The policy module decides when the
 * setup radios should be open; this file starts and stops them, joins Wi-Fi,
 * and publishes the device on the local network under the names hk_identity
 * derives.
 *
 * NOT YET VERIFIED ON HARDWARE. This compiles against ESP-IDF v5.5.1, but no
 * board has run it, and no iOS or Android client has been near it.
 *
 * One transport at a time
 * -----------------------
 * ESP-IDF's provisioning manager keeps a single static context and takes one
 * scheme, so BLE and SoftAP cannot both be live in one session: scheme_ble
 * puts Wi-Fi in station mode while scheme_softap needs AP+station.
 *
 * ADR-0005 resolves this by offering them in sequence, and which one opens is
 * decided by how provisioning was entered, never by the caller:
 *
 *   no stored credentials -> SoftAP
 *   button on a configured device -> BLE
 *
 * The reasoning is that the app-less path must always be reachable. Someone
 * setting a speaker up for the first time may have no app at all, so first boot
 * gets SoftAP. Someone pressing the button on a working speaker already has a
 * network, and a SoftAP would push their phone off it, so that path gets BLE. A
 * user who needs the app-less route on a configured device holds the button for
 * 5 s to clear the credentials, which lands them back in the first case.
 *
 * MEASURED ON HARDWARE 2026-09-05: the app-less half of that is not true yet.
 * Joining the SoftAP opens nothing. wifi_prov_scheme_softap serves protocomm
 * endpoints at 192.168.4.1, not a web page, so a phone that joins sits on a
 * network with no captive portal to redirect it, and HK_PORTAL_TITLE in
 * hk_identity.h names a page nothing serves. What exists today is the Espressif
 * provisioning app or ESP-IDF's esp_prov.py. The paragraph above is the design;
 * until a portal is served it should not be read as a description.
 *
 * The bring-up devkit has no button, so under the rule above its BLE transport
 * can never be reached, and a transport nothing can reach is one nothing can
 * test. CONFIG_HK_DEVKIT_FIRST_BOOT_BLE opens BLE there instead. It changes
 * nothing on the product board.
 *
 * Security
 * --------
 * Provisioning runs with Security 2 (SRP6a). The salt and verifier are
 * per-device and read from the factory_cal namespace; the device never stores
 * the password itself. If they are missing, provisioning does NOT start and
 * does NOT fall back to a weaker mode. A shared or absent credential on a
 * device that accepts Wi-Fi passwords is worse than no provisioning at all.
 */
#ifndef HK_NETWORK_H
#define HK_NETWORK_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * Which provisioning transport a session offers.
 *
 * Chosen by hk_network from the situation, not passed in. Exposed so it can be
 * logged and reasoned about.
 */
typedef enum {
    HK_NET_SCHEME_SOFTAP = 0, /**< App-less: SoftAP plus a captive portal */
    HK_NET_SCHEME_BLE,        /**< Espressif provisioning apps over BLE.
                                   Needs CONFIG_BT_ENABLED; without it the call
                                   fails with ESP_ERR_NOT_SUPPORTED rather than
                                   silently using another transport. */
} hk_net_scheme_t;

/** The transport that a given situation opens. See the note above for why. */
hk_net_scheme_t hk_network_scheme_for(bool has_credentials);

/** What the network layer is doing, for the status LED. */
typedef struct {
    bool provisioning;
    bool connecting;
    bool connected;
    bool error;
} hk_net_status_t;

/** Called whenever the status changes. Runs on the system event task. */
typedef void (*hk_net_status_cb_t)(const hk_net_status_t *status, void *context);

/**
 * Bring up netif, the event loop and Wi-Fi, then either join a stored network
 * or open provisioning on the transport that fits the situation.
 *
 * @param callback  status changes; may be NULL
 * @param context   passed back to the callback
 */
esp_err_t hk_network_start(hk_net_status_cb_t callback, void *context);

/**
 * Open a provisioning window from a button press.
 *
 * On a configured device this opens BLE. On one with no credentials
 * provisioning is already open over SoftAP and this does nothing, so a stray
 * press cannot tear down a setup session the user is in the middle of.
 */
esp_err_t hk_network_open_provisioning(void);

/** Forget stored Wi-Fi credentials and reopen provisioning. */
/**
 * Shut the setup radios.
 *
 * The counterpart to hk_network_open_provisioning(), and the reason the
 * bounded window in hk_provision means anything: without it the policy could
 * decide the window had expired and nothing would happen. A device left
 * advertising BLE all evening because nobody could close it is the failure
 * this prevents.
 *
 * Safe to call when provisioning is already shut.
 */
esp_err_t hk_network_close_provisioning(void);

esp_err_t hk_network_forget_credentials(void);

/** True when Wi-Fi credentials are stored. */
bool hk_network_is_provisioned(void);

#endif /* HK_NETWORK_H */
