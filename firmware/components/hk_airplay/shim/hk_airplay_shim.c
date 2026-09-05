/**
 * @file hk_airplay_shim.c
 * @brief The two functions the vendored receiver calls but whose owners we did
 *        not vendor.
 *
 * Upstream's Wi-Fi manager and LED driver are whole subsystems that collide
 * with hk_network and hk_ui, so neither is compiled. Between them the sources
 * that ARE compiled call exactly two of their functions, and those two are
 * implemented here rather than by dragging in the modules that declare them.
 *
 * That number is worth stating because it is the measure of how separable the
 * receiver turned out to be, and because it is a number that can grow: if a
 * future upstream commit adds a third, this file is where the link error
 * lands, and the choice between shimming it and vendoring its owner is made
 * here deliberately instead of by whoever is fixing the build that day.
 */

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"

#include "led.h"
#include "wifi.h"

/**
 * The device MAC as text, for the mDNS AirPlay records.
 *
 * Upstream reads it from its own Wi-Fi manager. Read here from the same eFuse
 * the rest of this firmware derives its identity from, so the AirPlay records
 * and hk_identity cannot disagree about which speaker this is.
 *
 * Upstream's own implementation writes the colon-separated upper-case form and
 * the mDNS TXT records are built from it, so the format is not a free choice.
 */
void wifi_get_mac_str(char *mac_str, size_t len)
{
    if (mac_str == NULL || len == 0u) {
        return;
    }
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        mac_str[0] = '\0';
        return;
    }
    const int written = snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (written < 0 || (size_t)written >= len) {
        mac_str[0] = '\0';
    }
}

/**
 * Audio samples offered to upstream's LED, for its VU meter mode.
 *
 * Dropped. The LED has one owner in this firmware -- hk_ui, which arbitrates
 * between boot, provisioning, network, fault and charge states under a
 * documented precedence -- and a second writer pushing an audio level into it
 * would not be a nicer LED, it would be a status indicator that sometimes lies
 * about faults. Whether AirPlay should be one of the states hk_ui arbitrates
 * is a UI decision, and it belongs in hk_ui rather than here.
 *
 * Called per output frame, so it must stay this cheap.
 */
void led_audio_feed(const int16_t *pcm, size_t stereo_samples)
{
    (void)pcm;
    (void)stereo_samples;
}
