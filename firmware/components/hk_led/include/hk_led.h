/**
 * @file hk_led.h
 * @brief RGB status LED: which state wins, and what it looks like.
 *
 * Implements the LED table in docs/controls-and-provisioning-plan.md. Pure C,
 * so the priority rules are testable without a board.
 *
 * Several conditions are usually true at once — the device can be playing,
 * on a low battery, and have the button held, all at the same moment. A single
 * LED can only say one thing, so the order is a decision, not an accident:
 *
 *   1. error            something is wrong and the user must know
 *   2. OTA              power must not be cut while flashing
 *   3. button feedback  the user is holding the button and needs to see which
 *                       action they are about to commit to
 *   4. battery low
 *   5. playing / ready / connecting / provisioning / boot
 *
 * Button feedback outranks battery low because the user is actively doing
 * something and a wrong guess is destructive; the battery warning is still
 * true a second later, when they let go.
 */
#ifndef HK_LED_H
#define HK_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "hk_button.h"

/** What the LED is currently saying. */
typedef enum {
    HK_LED_OFF = 0,
    HK_LED_BOOT,            /**< White breathe: starting up */
    HK_LED_PROVISIONING,    /**< Blue breathe: BLE/SoftAP open */
    HK_LED_CONNECTING,      /**< Yellow slow blink: joining Wi-Fi */
    HK_LED_READY,           /**< Green: connected, AirPlay ready */
    HK_LED_PLAYING,         /**< Purple, dim: audio playing */
    HK_LED_OTA,             /**< Cyan blink: updating, do not cut power */
    HK_LED_BATTERY_LOW,     /**< Red slow */
    HK_LED_ERROR,           /**< Red fast */
    HK_LED_HOLD_NETWORK,    /**< Yellow countdown: release clears Wi-Fi */
    HK_LED_HOLD_FACTORY,    /**< Red fast: release restores user settings */
} hk_led_state_t;

/** How the colour is animated. */
typedef enum {
    HK_LED_ANIM_SOLID = 0,
    HK_LED_ANIM_BREATHE,
    HK_LED_ANIM_BLINK_SLOW,
    HK_LED_ANIM_BLINK_FAST,
} hk_led_anim_t;

/** Everything the driver needs to render one state. */
typedef struct {
    uint8_t       red;
    uint8_t       green;
    uint8_t       blue;
    hk_led_anim_t animation;
    uint16_t      period_ms;    /**< Animation period; 0 when solid */
    uint8_t       brightness;   /**< Percent of full scale */
} hk_led_pattern_t;

/**
 * Brightness ceiling.
 *
 * These speakers sit in living rooms and bedrooms. A status LED at full
 * brightness is unpleasant at night, so nothing is ever driven above this
 * except the states the user must not miss.
 */
#define HK_LED_BRIGHTNESS_AMBIENT 25u
#define HK_LED_BRIGHTNESS_NORMAL  60u
#define HK_LED_BRIGHTNESS_ALERT   100u

/**
 * Floor of the breathe, as a fraction of 255.
 *
 * A breathe that reaches zero is not a breathe, it is a slow blink: the light
 * disappears, and what the eye notices is the disappearing rather than the
 * rhythm. Holding a floor keeps the colour present the whole cycle, which is
 * also what makes it readable as a state at a glance rather than something to
 * wait for.
 */
#define HK_LED_BREATHE_FLOOR      64u

/** Everything that could want the LED, sampled at one instant. */
typedef struct {
    bool             error;         /**< Wi-Fi, audio or battery fault */
    bool             ota;           /**< Update in progress */
    bool             battery_low;
    bool             playing;       /**< AirPlay audio active */
    bool             ready;         /**< Connected and announced */
    bool             connecting;    /**< Joining Wi-Fi */
    bool             provisioning;  /**< BLE/SoftAP open */
    bool             booting;
    hk_button_hold_t button_hold;
} hk_led_inputs_t;

/** Decide which single state the LED shows. */
hk_led_state_t hk_led_resolve(const hk_led_inputs_t *inputs);

/** Look up how a state is rendered. Never returns NULL. */
const hk_led_pattern_t *hk_led_pattern(hk_led_state_t state);

/** Short name, for logs and tests. */
const char *hk_led_state_name(hk_led_state_t state);

#endif /* HK_LED_H */
