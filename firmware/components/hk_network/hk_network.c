#include "hk_network.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <stdio.h>

#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
/* The BLE scheme only exists when Bluetooth is compiled in. Guarding it here
 * rather than always enabling Bluetooth keeps the radio, its flash footprint
 * and its RAM out of a build that uses the SoftAP path. */
#ifdef CONFIG_BT_ENABLED
#include "wifi_provisioning/scheme_ble.h"
#endif

#include "hk_identity.h"
#include "hk_provision.h"
#include "hk_storage.h"

static const char *TAG = "hk_net";

/**
 * Where per-device provisioning credentials live.
 *
 * Read through hk_storage rather than opened here. That module owns the
 * factory_cal partition, opens it read-only, and is the single place the
 * PRD-008 wall is enforced; a second opener would be a second place to get it
 * wrong. An earlier version of this file called nvs_open("factory_cal", ...),
 * which opens a NAMESPACE of that name inside the DEFAULT partition — so the
 * credentials would have sat in the user-settings partition, where the
 * reformat-on-corruption path erases everything.
 */
#define HK_PROV_NVS_SALT      "prov_salt"
#define HK_PROV_NVS_VERIFIER  "prov_verif"

/*
 * SRP6a salt and verifier sizes.
 *
 * These are UPPER BOUNDS, not fixed sizes, and the difference matters. The
 * generator derives both from big integers and serialises them with the minimal
 * number of bytes, so a value whose top byte happens to be zero comes out one
 * byte short. Measured over 600 generations: the salt was 15 bytes once and the
 * verifier 383 bytes four times, both close to the 1-in-256 the arithmetic
 * predicts.
 *
 * The bytes are used verbatim on both sides — the generator hashes the salt as
 * a raw byte string when computing the verifier — so padding a short one to a
 * fixed width here would silently break the handshake on roughly one device in
 * every 256. Whatever length was stored is the length that must be used.
 */
#define HK_PROV_SALT_MAX 16
#define HK_PROV_VERIFIER_MAX 384

/* Below these, something other than a credential was stored. */
#define HK_PROV_SALT_MIN 8
#define HK_PROV_VERIFIER_MIN 256

/** Consecutive join attempts before the policy module is told it failed. */
#define HK_NET_RETRY_LIMIT 5

static hk_identity_t      s_identity;
static hk_net_scheme_t    s_scheme;
static hk_net_status_cb_t s_callback;
static void              *s_context;
static hk_net_status_t    s_status;
static int                s_retries;
static uint8_t            s_salt[HK_PROV_SALT_MAX];
static uint8_t            s_verifier[HK_PROV_VERIFIER_MAX];
static size_t             s_salt_len;
static size_t             s_verifier_len;
static bool               s_have_security2;

static void publish_status(void)
{
    if (s_callback != NULL) {
        s_callback(&s_status, s_context);
    }
}

/**
 * Load the per-device SRP6a salt and verifier.
 *
 * These are written once at manufacturing time, together with the QR label.
 * The password itself is never stored on the device, which is the point of
 * Security 2: reading the flash does not yield the credential.
 */
static esp_err_t load_security2_credentials(void)
{
    s_salt_len = sizeof(s_salt);
    s_verifier_len = sizeof(s_verifier);

    esp_err_t err = hk_storage_factory_get_blob(HK_PROV_NVS_SALT, s_salt, &s_salt_len);
    if (err == ESP_OK) {
        err = hk_storage_factory_get_blob(HK_PROV_NVS_VERIFIER, s_verifier, &s_verifier_len);
    }
    if (err != ESP_OK) {
        return err;
    }

    /* A range, not an equality. See the note on HK_PROV_SALT_MAX: demanding an
     * exact length would reject roughly one device in 256 for no reason. */
    if (s_salt_len < HK_PROV_SALT_MIN || s_salt_len > HK_PROV_SALT_MAX ||
        s_verifier_len < HK_PROV_VERIFIER_MIN || s_verifier_len > HK_PROV_VERIFIER_MAX) {
        ESP_LOGE(TAG, "provisioning credentials are implausible: salt %u B, verifier %u B",
                 (unsigned)s_salt_len, (unsigned)s_verifier_len);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "provisioning credentials loaded: salt %u B, verifier %u B",
             (unsigned)s_salt_len, (unsigned)s_verifier_len);
    return ESP_OK;
}

/** Publish the device on the local network under its documented names. */
static esp_err_t start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }
    err = mdns_hostname_set(s_identity.mdns);
    if (err != ESP_OK) {
        return err;
    }
    return mdns_instance_name_set(s_identity.airplay);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) {
        return;
    }

    switch (id) {
    case WIFI_EVENT_STA_START:
        s_status.connecting = true;
        publish_status();
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        s_status.connected = false;
        if (s_retries < HK_NET_RETRY_LIMIT) {
            s_retries++;
            s_status.connecting = true;
            ESP_LOGW(TAG, "disconnected, retry %d of %d", s_retries, HK_NET_RETRY_LIMIT);
            esp_wifi_connect();
        } else {
            /* Out of retries. The policy module decides what happens next;
             * this layer only reports. */
            s_status.connecting = false;
            s_status.error = true;
            ESP_LOGE(TAG, "could not join after %d attempts", HK_NET_RETRY_LIMIT);
        }
        publish_status();
        break;

    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "joined, address " IPSTR, IP2STR(&event->ip_info.ip));

    s_retries = 0;
    s_status.connecting = false;
    s_status.connected = true;
    s_status.error = false;
    publish_status();

    if (start_mdns() != ESP_OK) {
        ESP_LOGW(TAG, "mdns did not start; the speaker will not be discoverable by name");
    }
}

static void on_prov_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_PROV_EVENT) {
        return;
    }

    switch (id) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "provisioning open over %s",
                 s_scheme == HK_NET_SCHEME_BLE ? "ble" : "softap");
        s_status.provisioning = true;
        publish_status();
        break;

    case WIFI_PROV_CRED_RECV:
        ESP_LOGI(TAG, "credentials received");
        break;

    case WIFI_PROV_CRED_FAIL:
        /* The password was wrong or the network was unreachable. The radios
         * stay up so the user can see the result and try again. */
        ESP_LOGE(TAG, "provisioning failed; setup stays open");
        s_status.error = true;
        publish_status();
        break;

    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning succeeded");
        s_status.error = false;
        publish_status();
        break;

    case WIFI_PROV_END:
        /* Everything the manager allocated goes back, including the BLE stack
         * when the BLE scheme was used. ADR-0005 requires that it not be left
         * running during normal operation. */
        wifi_prov_mgr_deinit();
        s_status.provisioning = false;
        publish_status();
        ESP_LOGI(TAG, "provisioning closed and its memory released");
        break;

    default:
        break;
    }
}

/** Start provisioning with Security 2, or refuse. */
static esp_err_t start_provisioning(void)
{
    if (!s_have_security2) {
        /* Deliberately fatal to provisioning rather than a downgrade. Accepting
         * a Wi-Fi password over a channel secured by a shared or absent
         * credential is worse than refusing to accept one at all. */
        ESP_LOGE(TAG, "no per-device provisioning credentials in the calibration store; "
                      "refusing to open provisioning rather than fall back to a weaker "
                      "security mode");
        return ESP_ERR_NOT_FOUND;
    }

    /* The stored lengths, not the buffer sizes. Passing sizeof() here would
     * hand protocomm trailing zero bytes that were never part of the salt. */
    wifi_prov_security2_params_t security_params = {
        .salt = (const char *)s_salt,
        .salt_len = (uint16_t)s_salt_len,
        .verifier = (const char *)s_verifier,
        .verifier_len = (uint16_t)s_verifier_len,
    };

    /* service_key is the SoftAP password. NULL leaves the setup network open,
     * which the captive-portal flow needs; the session itself is protected by
     * Security 2, so the password never crosses in clear text. */
    return wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_2, &security_params,
                                            s_identity.softap, NULL);
}

hk_net_scheme_t hk_network_scheme_for(bool has_credentials)
{
    /* ADR-0005 option C. The app-less path must always be reachable, so a
     * device with nothing stored opens SoftAP; a device that already works
     * opens BLE, because a SoftAP would push the user's phone off the network
     * they are on. Clearing credentials with a 5 s hold returns them to the
     * SoftAP case, which is how the app-less route stays available. */
    return has_credentials ? HK_NET_SCHEME_BLE : HK_NET_SCHEME_SOFTAP;
}

static esp_err_t init_provisioning_manager(void)
{
    if (s_scheme == HK_NET_SCHEME_BLE) {
#ifdef CONFIG_BT_ENABLED
        /* FREE_BTDM releases the whole Bluetooth stack once provisioning ends,
         * which is what ADR-0005 requires: BLE must not stay up during normal
         * playback. */
        wifi_prov_mgr_config_t config = {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        };
        return wifi_prov_mgr_init(config);
#else
        ESP_LOGE(TAG, "the BLE provisioning scheme needs CONFIG_BT_ENABLED, which this "
                      "build does not set");
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    return wifi_prov_mgr_init(config);
}

#if CONFIG_HK_BOARD_DEVKIT_N8R2
/**
 * Put the bench board on a network over USB, because the two normal ways in are
 * both closed on it.
 *
 * The devkit has no button, so the press that clears credentials cannot be
 * given; and joining its SoftAP from the machine that is driving it takes that
 * machine off the network it is being told to join. Neither is a firmware
 * problem and neither is worth a firmware workaround on the product, so this
 * exists only where those two facts hold.
 *
 * It never overwrites credentials that are already stored. A preload that
 * silently replaced a provisioned network would make every bench result
 * ambiguous: nobody could tell which network a board was actually on.
 *
 * The values come from a build-time config that is deliberately empty in the
 * committed tree; see main/Kconfig.projbuild. The password does end up inside
 * the image, which is the honest cost of doing this at all, and the reason it
 * is confined to a board with nothing attached to it.
 */
static void preload_bench_credentials(void)
{
    if (CONFIG_HK_DEVKIT_WIFI_SSID[0] == '\0') {
        return;
    }

    wifi_config_t existing = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &existing) == ESP_OK
        && existing.sta.ssid[0] != '\0') {
        ESP_LOGI(TAG, "bench preload skipped: this board already has a network");
        return;
    }

    wifi_config_t bench = {0};
    (void)snprintf((char *)bench.sta.ssid, sizeof(bench.sta.ssid),
                   "%s", CONFIG_HK_DEVKIT_WIFI_SSID);
    (void)snprintf((char *)bench.sta.password, sizeof(bench.sta.password),
                   "%s", CONFIG_HK_DEVKIT_WIFI_PASSWORD);

    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &bench);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bench preload failed: %s", esp_err_to_name(err));
        return;
    }
    /* The SSID is not a secret and naming it is the point: it says which
     * network this board was put on, which is the first thing to check when a
     * bench result disagrees with expectations. */
    ESP_LOGW(TAG, "bench preload wrote a network for '%s'; this build carries it",
             (const char *)bench.sta.ssid);
}
#endif

esp_err_t hk_network_start(hk_net_status_cb_t callback, void *context)
{
    s_callback = callback;
    s_context = context;
    memset(&s_status, 0, sizeof(s_status));
    s_retries = 0;

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read mac");
    if (hk_identity_from_mac(mac, &s_identity) != HK_IDENTITY_OK) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();
    /* The AP interface is created unconditionally: whether SoftAP is needed
     * depends on stored credentials, which cannot be read until the manager is
     * up, and creating a netif is cheap next to discovering too late that it is
     * missing. */
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   on_wifi_event, NULL), TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   on_ip_event, NULL), TAG, "ip events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                   on_prov_event, NULL), TAG, "prov events");

#if CONFIG_HK_BOARD_DEVKIT_N8R2
    preload_bench_credentials();
#endif

    /* Named for what it holds, a status, not for what was being loaded. A
     * variable called `credentials` reaching a log line is exactly the shape
     * tools/check_no_credential_logs.py is looking for, and it was right to
     * object even though nothing secret was printed. */
    esp_err_t load_status = load_security2_credentials();
    s_have_security2 = (load_status == ESP_OK);
    if (!s_have_security2) {
        ESP_LOGE(TAG, "provisioning credentials unavailable: %s. This device cannot be "
                      "provisioned until they are written at manufacturing time.",
                 esp_err_to_name(load_status));
    }

    /* The manager is needed just to answer "are we provisioned?", and the
     * answer decides the scheme. It is initialised with SoftAP for that query
     * and reinitialised below if BLE turns out to be the right transport. */
    s_scheme = HK_NET_SCHEME_SOFTAP;
    ESP_RETURN_ON_ERROR(init_provisioning_manager(), TAG, "prov mgr init");

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_is_provisioned(&provisioned), TAG, "is provisioned");
    s_scheme = hk_network_scheme_for(provisioned);

    if (provisioned) {
        /* Nothing to set up. Release the manager and just join. */
        wifi_prov_mgr_deinit();
        ESP_LOGI(TAG, "credentials found, joining as %s", s_identity.mdns);
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
        return ESP_OK;
    }

    esp_err_t err = start_provisioning();
    if (err != ESP_OK) {
        wifi_prov_mgr_deinit();
    }
    return err;
}

esp_err_t hk_network_open_provisioning(void)
{
    if (s_status.provisioning) {
        /* Already open. A stray press must not tear down a setup session the
         * user is in the middle of. */
        ESP_LOGI(TAG, "provisioning is already open; leaving it alone");
        return ESP_OK;
    }

    s_scheme = hk_network_scheme_for(hk_network_is_provisioned());
    ESP_LOGI(TAG, "opening provisioning over %s",
             s_scheme == HK_NET_SCHEME_BLE ? "ble" : "softap");

    ESP_RETURN_ON_ERROR(init_provisioning_manager(), TAG, "prov mgr init");
    esp_err_t err = start_provisioning();
    if (err != ESP_OK) {
        wifi_prov_mgr_deinit();
    }
    return err;
}

esp_err_t hk_network_close_provisioning(void)
{
    if (!s_status.provisioning) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "closing provisioning");
    /* stop_provisioning() before deinit(): ESP-IDF's own documentation warns
     * that deinit alone leaves the transport running. */
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_deinit();
    s_status.provisioning = false;
    publish_status();
    return ESP_OK;
}

esp_err_t hk_network_forget_credentials(void)
{
    ESP_LOGW(TAG, "forgetting stored Wi-Fi credentials");
    /* Clears only the Wi-Fi credentials the manager stored. Factory
     * calibration lives in its own partition and is untouched (PRD-008). */
    ESP_RETURN_ON_ERROR(esp_wifi_restore(), TAG, "wifi restore");
    return hk_network_open_provisioning();
}

bool hk_network_is_provisioned(void)
{
    bool provisioned = false;
    return wifi_prov_mgr_is_provisioned(&provisioned) == ESP_OK && provisioned;
}
