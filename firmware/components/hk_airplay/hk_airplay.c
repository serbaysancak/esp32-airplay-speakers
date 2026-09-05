/**
 * @file hk_airplay.c
 * @brief Bringing up the vendored AirPlay 2 receiver.
 *
 * This is upstream's start_airplay_services() rewritten against this project's
 * decisions rather than copied. The order of the calls is upstream's, because
 * it is load-bearing -- the PTP clock has to exist before the receiver, the
 * receiver before the RTSP server that hands sessions to it -- and the
 * departures from it are the point of the file:
 *
 *   - the device name is published from hk_identity, so the speaker that
 *     appears on an iPhone is the same one the boot log and the SoftAP name
 *     describe;
 *   - nothing here erases NVS, brings up Wi-Fi, opens a web server or drives a
 *     LED, all of which upstream's app_main does and all of which this firmware
 *     already owns;
 *   - it refuses to start on a board that has no business driving I2S.
 */

#include "hk_airplay.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#include "hk_identity.h"
#include "hk_pins.h"
#include "hk_storage.h"

/* Vendored, and reachable only through PRIV_INCLUDE_DIRS: nothing outside this
 * component gets to depend on upstream's headers. */
#include "audio_output.h"
#include "audio_receiver.h"
#include "hap.h"
#include "mdns_airplay.h"
#include "playback_control.h"
#include "ptp_clock.h"
#include "rtsp_events.h"
#include "rtsp_server.h"
#include "settings.h"

static const char *TAG = "hk_airplay";

/*
 * The receiver's I2S pins come from its own Kconfig, and this project's pin
 * assignment comes from hk_pins.h. Two places naming the same three pins is
 * two places to change, so the compiler is told they must agree -- a silent
 * disagreement would clock audio out of whichever pins hk_pins reserved for
 * something else, which on the product board means I2C to the DAC.
 */
_Static_assert(CONFIG_I2S_BCK_IO == HK_PIN_I2S_BCLK,
               "AirPlay bit clock does not match hk_pins");
_Static_assert(CONFIG_I2S_WS_IO == HK_PIN_I2S_LRCLK,
               "AirPlay word select does not match hk_pins");
_Static_assert(CONFIG_I2S_DO_IO == HK_PIN_I2S_DATA,
               "AirPlay data output does not match hk_pins");

#if CONFIG_HK_AIRPLAY_OUTPUT_SPDIF
/* S/PDIF replaces the I2S output rather than joining it, so it lands on the
 * same reserved pin. Asserted for the same reason as the three above: a pin
 * named in two places is a pin that can disagree with itself. */
_Static_assert(CONFIG_SPDIF_DO_IO == HK_PIN_I2S_DATA,
               "S/PDIF output does not match hk_pins");
#endif

static bool                  s_running;
static hk_airplay_state_cb_t s_on_state;
static void                 *s_state_context;

/**
 * Translate the receiver's own events into one bit: is audio playing.
 *
 * CLIENT_CONNECTED is deliberately not playing. A phone that has selected this
 * speaker but not started a track has a session open and no audio in it, and a
 * status light that says otherwise is a light that has to be distrusted.
 *
 * Runs on the RTSP task, so it does nothing but forward.
 */
static void on_rtsp_event(rtsp_event_t event, const rtsp_event_data_t *data,
                          void *user_data)
{
    (void)data;
    (void)user_data;
    if (s_on_state == NULL) {
        return;
    }
    switch (event) {
    case RTSP_EVENT_PLAYING:
        s_on_state(true, s_state_context);
        break;
    case RTSP_EVENT_PAUSED:
    case RTSP_EVENT_DISCONNECTED:
        s_on_state(false, s_state_context);
        break;
    case RTSP_EVENT_CLIENT_CONNECTED:
    case RTSP_EVENT_METADATA:
    default:
        break;
    }
}

/**
 * Whether this build may clock the I2S pins at all.
 *
 * The receiver's output stage is not optional: without it there is nothing to
 * hand a stream to. So the question is asked once, here, before anything
 * starts.
 *
 * On the bring-up devkit the answer is yes and the reason is physical -- there
 * is no DAC and no amplifier on it, so I2S drives nothing but a header. On a
 * product board the answer is whatever hk_storage says, which is no until G0
 * and G2 have produced a driver-protection profile. That is the same gate the
 * rest of the audio path answers to; AirPlay does not get its own weaker one.
 */
static bool audio_may_run(void)
{
#if CONFIG_HK_BOARD_DEVKIT_N8R2
    return true;
#else
    return hk_storage_audio_permitted();
#endif
}

/** Publish this speaker's name to the receiver, if it is not already right. */
static void publish_device_name(void)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGW(TAG, "no MAC; leaving the receiver's stored name alone");
        return;
    }
    hk_identity_t identity;
    if (hk_identity_from_mac(mac, &identity) != 0) {
        ESP_LOGW(TAG, "identity did not derive; leaving the stored name alone");
        return;
    }

    /* Read before writing. settings_set_device_name() writes NVS, and this runs
     * on every join; writing an unchanged name every boot would spend flash
     * endurance to store what is already there. */
    char stored[64] = {0};
    if (settings_get_device_name(stored, sizeof(stored)) == ESP_OK
        && strcmp(stored, identity.airplay) == 0) {
        return;
    }
    if (settings_set_device_name(identity.airplay) != ESP_OK) {
        ESP_LOGW(TAG, "could not store the device name; the receiver will "
                      "advertise its own default instead of %s", identity.airplay);
    }
}

esp_err_t hk_airplay_start(hk_airplay_state_cb_t on_state, void *context)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_on_state = on_state;
    s_state_context = context;
    if (!audio_may_run()) {
        ESP_LOGE(TAG, "not starting: this board has no driver-protection profile, "
                      "and the receiver drives I2S. G0/G2 come first.");
        return ESP_ERR_NOT_ALLOWED;
    }

    esp_err_t err = settings_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "receiver settings did not open: %s", esp_err_to_name(err));
        return err;
    }
    publish_device_name();

    err = playback_control_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "playback control: %s", esp_err_to_name(err));
        return err;
    }

    /* ESP_ERR_INVALID_STATE means it is already running, which is not a
     * failure -- upstream treats it the same way. */
    err = ptp_clock_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ptp clock: %s", esp_err_to_name(err));
        return err;
    }

    err = hap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hap: %s", esp_err_to_name(err));
        return err;
    }
    err = audio_receiver_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio receiver: %s", esp_err_to_name(err));
        return err;
    }
    err = audio_output_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio output: %s", esp_err_to_name(err));
        return err;
    }

    /* Publishes the AirPlay records. mDNS itself is already running -- hk_network
     * started it for the speaker's own hostname -- and mdns_init() returns
     * ESP_OK when it is, so this adds services rather than replacing them. */
    mdns_airplay_init();

    audio_output_start();

    err = rtsp_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rtsp server: %s", esp_err_to_name(err));
        return err;
    }

    if (s_on_state != NULL && rtsp_events_register(on_rtsp_event, NULL) != 0) {
        /* Not fatal. The receiver works; only the status light goes quiet, and
         * a speaker that plays without lighting up is better than one that
         * refuses to start because it could not light up. */
        ESP_LOGW(TAG, "no room to listen for playback events; the status LED "
                      "will not show playback");
    }

    playback_control_set_source(PLAYBACK_SOURCE_AIRPLAY);
    s_running = true;

    /* Said plainly, because the boot report a few seconds earlier printed
     * "output SILENT (i2s=0 dac=0 amp=0)" and that stops being true here. It
     * was accurate when it was printed; leaving it as the last word on the
     * subject would make the log claim something the device is no longer doing.
     *
     * What actually changed is only the first of the three: the receiver clocks
     * the I2S pins. hk_audio still owns the DAC and amplifier mute lines and
     * still holds them asserted, because audio is still not permitted -- and on
     * this board there is nothing on the other end of those pins anyway. */
#if CONFIG_HK_AIRPLAY_OUTPUT_SPDIF
    ESP_LOGI(TAG, "receiver ready; audio leaves as S/PDIF on gpio%d. "
                  "The DAC and amplifier lines stay muted -- this output does "
                  "not go through them.", CONFIG_SPDIF_DO_IO);
#else
    ESP_LOGI(TAG, "receiver ready; I2S is clocked from here on, "
                  "the DAC and amplifier stay muted");
#endif
    return ESP_OK;
}

bool hk_airplay_is_running(void)
{
    return s_running;
}
