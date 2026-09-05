/**
 * @file hk_ui.h
 * @brief Drives the function button and the RGB status LED.
 *
 * The thin hardware layer under two tested policy modules: hk_button decides
 * what a press means, hk_led decides what the LED should say, and this file
 * does nothing but read a pin, drive three PWM channels, and pass events on.
 *
 * It runs in its own low-priority task. The controls plan is explicit that LED
 * animation must never share a task with audio: a blocking or long-running UI
 * step there would show up as an I2S underrun.
 *
 * NOT YET VERIFIED ON HARDWARE. This compiles against ESP-IDF v5.5.1 and its
 * logic is covered by the host tests behind it, but no board has run it. The
 * PWM frequency in particular is a reasoned choice, not a measured one; see the
 * note on HK_UI_LED_PWM_HZ.
 */
#ifndef HK_UI_H
#define HK_UI_H

#include <stdbool.h>

#include "esp_err.h"
#include "hk_button.h"
#include "hk_led.h"

/**
 * LED PWM carrier frequency.
 *
 * Chosen above the audible band on purpose. The LED lines run through the same
 * enclosure as an analogue audio path and a Class-D amplifier, and a carrier
 * inside the audio band would be audible if it coupled at all. 25 kHz also
 * keeps 10-bit resolution available from the 80 MHz APB clock.
 *
 * This is reasoning, not measurement. The G3 noise measurement decides the
 * final value, and it may well move.
 */
#define HK_UI_LED_PWM_HZ 25000

/** How often the button is sampled. Well below the 50 ms debounce interval. */
#define HK_UI_POLL_MS 10

/** Called from the UI task when the button commits an action. */
typedef void (*hk_ui_event_cb_t)(hk_button_event_t event, void *context);

/**
 * Configure the pins and start the UI task.
 *
 * @param callback  invoked from the UI task on each committed button action;
 *                  keep it short and do not block in it
 * @param context   passed back to the callback
 */
esp_err_t hk_ui_start(hk_ui_event_cb_t callback, void *context);

/**
 * Tell the UI what the rest of the firmware is doing.
 *
 * The button hold level is filled in by the UI task itself and is ignored here.
 * Safe to call from any task.
 */
/**
 * Sources that can raise the fault indication.
 *
 * A bitmask rather than a bool because the LED's single red state is shared by
 * several unrelated subsystems. If it were one flag, whichever of them wrote
 * last would decide, and clearing a network fault would quietly clear an audio
 * one. Each source owns its bit and can only speak for itself.
 */
typedef enum {
    HK_UI_FAULT_NETWORK = 1u << 0,
    HK_UI_FAULT_AUDIO   = 1u << 1,
    HK_UI_FAULT_POWER   = 1u << 2,
    HK_UI_FAULT_UPDATE  = 1u << 3,
} hk_ui_fault_t;

/*
 * Status is set field by field, by whoever owns that field.
 *
 * There is deliberately no "set the whole status" call. There used to be, and
 * every caller built a fresh struct with a few fields filled in, so each one
 * silently zeroed everything it did not know about. The update indication —
 * the one that tells someone not to pull the power while flash is being
 * written — would have been switched off by the next routine Wi-Fi event, and
 * nothing anywhere would have reported it.
 */

/** The network layer's own fields. Owns nothing else. */
void hk_ui_set_network(bool provisioning, bool connecting, bool ready);

/** Raise or clear one source's contribution to the fault indication. */
void hk_ui_set_fault(hk_ui_fault_t source, bool active);

/** An update is writing flash. Do not interrupt the power. */
void hk_ui_set_ota(bool active);

/** Playback is running. */
void hk_ui_set_playing(bool playing);

/** The pack is below its warning threshold. */
void hk_ui_set_battery_low(bool low);

/** Boot is finished; stop showing the boot state. */
void hk_ui_clear_booting(void);

/**
 * Whether the button was already held when hk_ui_start() ran.
 *
 * Valid only after hk_ui_start() returns ESP_OK.
 */
bool hk_ui_recovery_requested(void);

/**
 * Bytes still unused on the UI task's stack, at its worst point so far.
 *
 * The UI task is deliberately small: it reads one pin and drives three PWM
 * channels. Anything heavier belongs on the main task, and that is a rule
 * rather than a preference because the button callback used to open
 * provisioning inline -- NimBLE, protocomm and an SRP6a handshake on a stack
 * sized for debouncing -- and took the device down with it.
 *
 * Reported rather than assumed, so the margin is a number in the boot log and a
 * future addition that eats it becomes visible before it is fatal. Returns 0
 * before the task exists.
 */
size_t hk_ui_stack_headroom(void);

#endif /* HK_UI_H */
