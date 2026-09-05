#include "hk_led.h"

#include <stddef.h>

hk_led_state_t hk_led_resolve(const hk_led_inputs_t *inputs)
{
    if (inputs == NULL) {
        return HK_LED_OFF;
    }

    /* A fault the user has to act on outranks everything. */
    if (inputs->error) {
        return HK_LED_ERROR;
    }
    /* Cutting power during an update can leave a half-written slot, so this
     * warning outranks anything that is merely informational. */
    if (inputs->ota) {
        return HK_LED_OTA;
    }

    /* The user is holding the button right now and is about to commit to
     * something destructive. Show them which one. */
    switch (inputs->button_hold) {
    case HK_BUTTON_HOLD_FACTORY_ARMED:
        return HK_LED_HOLD_FACTORY;
    case HK_BUTTON_HOLD_NETWORK_ARMED:
        return HK_LED_HOLD_NETWORK;
    default:
        break;
    }

    if (inputs->battery_low) {
        return HK_LED_BATTERY_LOW;
    }
    if (inputs->playing) {
        return HK_LED_PLAYING;
    }
    if (inputs->ready) {
        return HK_LED_READY;
    }
    if (inputs->connecting) {
        return HK_LED_CONNECTING;
    }
    if (inputs->provisioning) {
        return HK_LED_PROVISIONING;
    }
    if (inputs->booting) {
        return HK_LED_BOOT;
    }
    return HK_LED_OFF;
}

static const hk_led_pattern_t PATTERNS[] = {
    [HK_LED_OFF]          = {0,   0,   0,   HK_LED_ANIM_SOLID,      0, 0},
    [HK_LED_BOOT]         = {255, 255, 255, HK_LED_ANIM_BREATHE, 2000, HK_LED_BRIGHTNESS_NORMAL},
    [HK_LED_PROVISIONING] = {0,   80,  255, HK_LED_ANIM_BREATHE, 2000, HK_LED_BRIGHTNESS_NORMAL},
    [HK_LED_CONNECTING]   = {255, 200, 0,   HK_LED_ANIM_BLINK_SLOW, 1000, HK_LED_BRIGHTNESS_NORMAL},
    [HK_LED_READY]        = {0,   255, 60,  HK_LED_ANIM_SOLID,      0, HK_LED_BRIGHTNESS_NORMAL},
    /* Playing is the long-lived state in a dark room, so it is the dimmest, and
     * it breathes slowly rather than sitting still: a steady dot reads as a
     * standby lamp, while a slow rise and fall reads as something running.
     * Slower than the boot and provisioning breathe, because those are asking
     * to be watched and this one is not. */
    [HK_LED_PLAYING]      = {160, 0,   255, HK_LED_ANIM_BREATHE, 3000, HK_LED_BRIGHTNESS_AMBIENT},
    [HK_LED_OTA]          = {0,   220, 255, HK_LED_ANIM_BLINK_SLOW, 800, HK_LED_BRIGHTNESS_ALERT},
    [HK_LED_BATTERY_LOW]  = {255, 0,   0,   HK_LED_ANIM_BLINK_SLOW, 2000, HK_LED_BRIGHTNESS_NORMAL},
    [HK_LED_ERROR]        = {255, 0,   0,   HK_LED_ANIM_BLINK_FAST, 250, HK_LED_BRIGHTNESS_ALERT},
    [HK_LED_HOLD_NETWORK] = {255, 200, 0,   HK_LED_ANIM_BLINK_FAST, 400, HK_LED_BRIGHTNESS_ALERT},
    [HK_LED_HOLD_FACTORY] = {255, 0,   0,   HK_LED_ANIM_BLINK_FAST, 150, HK_LED_BRIGHTNESS_ALERT},
};

const hk_led_pattern_t *hk_led_pattern(hk_led_state_t state)
{
    if ((unsigned)state >= (sizeof(PATTERNS) / sizeof(PATTERNS[0]))) {
        return &PATTERNS[HK_LED_OFF];
    }
    return &PATTERNS[state];
}

const char *hk_led_state_name(hk_led_state_t state)
{
    switch (state) {
    case HK_LED_OFF:          return "off";
    case HK_LED_BOOT:         return "boot";
    case HK_LED_PROVISIONING: return "provisioning";
    case HK_LED_CONNECTING:   return "connecting";
    case HK_LED_READY:        return "ready";
    case HK_LED_PLAYING:      return "playing";
    case HK_LED_OTA:          return "ota";
    case HK_LED_BATTERY_LOW:  return "battery_low";
    case HK_LED_ERROR:        return "error";
    case HK_LED_HOLD_NETWORK: return "hold_network";
    case HK_LED_HOLD_FACTORY: return "hold_factory";
    }
    return "unknown";
}
