/**
 * @file hk_airplay.h
 * @brief Starting the vendored AirPlay 2 receiver (ADR-0007).
 *
 * The receiver itself lives under components/hk_airplay/vendor, copied from
 * rbouteiller/airplay-esp32 at the commit ADR-0007 pins and not modified. That
 * project is an application rather than a library: it has its own app_main
 * which erases NVS, drives an LED, brings up Wi-Fi and serves a captive portal.
 * This firmware already owns all four, so none of it is taken -- the sources
 * behind it are simply not compiled, and hk_airplay_start() re-implements the
 * one function that brings the receiver up.
 *
 * Licence: the vendored code is under a non-commercial licence, and enabling
 * CONFIG_HK_AIRPLAY binds this whole firmware to those terms. The licence text
 * is kept beside the code it covers, at vendor/LICENSE.
 */
#ifndef HK_AIRPLAY_H
#define HK_AIRPLAY_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * Told when playback starts and stops.
 *
 * The receiver knows; the LED belongs to hk_ui; and neither should have to know
 * about the other. So the fact travels the way the network's status does --
 * out to whoever started the receiver, which decides what it means.
 *
 * Called from the RTSP task. Do the least possible in it.
 */
typedef void (*hk_airplay_state_cb_t)(bool playing, void *context);

/**
 * Bring the receiver up. Call once, after the network has an address.
 *
 * Refuses, rather than starting muted, when this device has no business driving
 * I2S: the receiver's output stage clocks real pins, and on a product board
 * without a calibration profile that is the thing G0 and G2 exist to prevent.
 * See the implementation for exactly which builds are allowed.
 *
 * @param on_state  Optional. Called when playback starts, pauses or ends.
 * @param context   Passed back to the callback.
 *
 * @return ESP_ERR_INVALID_STATE if it has already started, ESP_ERR_NOT_ALLOWED
 *         if this build may not drive audio, otherwise whatever the receiver's
 *         own initialisation returned.
 */
esp_err_t hk_airplay_start(hk_airplay_state_cb_t on_state, void *context);

/** Whether the receiver is running. */
bool hk_airplay_is_running(void);

#endif /* HK_AIRPLAY_H */
