#include "hk_ui.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hk_pins.h"

#if CONFIG_HK_DEVKIT_STATUS_LED
#include "led_strip.h"
#endif

static const char *TAG = "hk_ui";

/* 10 bits at 25 kHz: the APB clock is 80 MHz, so 80e6 / 25e3 = 3200 counts are
 * available and 2^10 = 1024 fits with margin. */
#define HK_UI_DUTY_BITS  LEDC_TIMER_10_BIT
#define HK_UI_DUTY_MAX   ((1u << 10) - 1u)

#define HK_UI_TASK_STACK 3072
/* Deliberately low. Audio and networking must both pre-empt the LED. */
#define HK_UI_TASK_PRIO  2

static TaskHandle_t s_task;

#if CONFIG_HK_DEVKIT_STATUS_LED
static led_strip_handle_t s_strip;

/** Bring up the devkit's on-board LED. Never fatal: it is a mirror. */
static void start_onboard_mirror(void)
{
    const led_strip_config_t strip = {
        .strip_gpio_num = CONFIG_HK_DEVKIT_STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    const esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "on-board LED on gpio%d did not start: %s. The external RGB "
                      "channels are unaffected.",
                 CONFIG_HK_DEVKIT_STATUS_LED_GPIO, esp_err_to_name(err));
        s_strip = NULL;
        return;
    }
    (void)led_strip_clear(s_strip);
    ESP_LOGI(TAG, "on-board status LED mirrored on gpio%d",
             CONFIG_HK_DEVKIT_STATUS_LED_GPIO);
}
#endif

typedef struct {
    ledc_channel_t channel;
    int            gpio;
} led_channel_t;

static const led_channel_t LED_CHANNELS[3] = {
    {LEDC_CHANNEL_0, HK_PIN_LED_R},
    {LEDC_CHANNEL_1, HK_PIN_LED_G},
    {LEDC_CHANNEL_2, HK_PIN_LED_B},
};

static hk_button_t       s_button;
static hk_led_inputs_t   s_status;
static uint32_t          s_faults;   /**< Bitmask of hk_ui_fault_t */
static portMUX_TYPE      s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static hk_ui_event_cb_t  s_callback;
static void             *s_context;
static bool              s_recovery;

/** Milliseconds since boot. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/** The button is active low: pressed pulls the pin to ground. */
static bool button_pressed(void)
{
    return gpio_get_level((gpio_num_t)HK_PIN_BUTTON) == 0;
}

/**
 * Animation envelope, 0-255, from the pattern and the current time.
 *
 * Kept in integer arithmetic so the task never touches the FPU: a task that
 * uses floating point forces the scheduler to save and restore FPU context on
 * every switch, which is a strange cost to pay for a blinking LED.
 */
static uint32_t envelope(const hk_led_pattern_t *pattern, uint32_t time_ms)
{
    if (pattern->animation == HK_LED_ANIM_SOLID || pattern->period_ms == 0) {
        return 255u;
    }
    uint32_t phase = time_ms % pattern->period_ms;

    if (pattern->animation == HK_LED_ANIM_BREATHE) {
        /* A raised cosine, not a triangle.
         *
         * A triangle turns around instantly at both ends, and the eye sees
         * those corners as a flick rather than a breath -- the light appears to
         * stall at the top and snap at the bottom. A cosine has zero slope at
         * both turning points, which is what makes it read as breathing.
         *
         * It runs between HK_LED_BREATHE_FLOOR and full rather than from zero,
         * so the colour never leaves. */
        const float turn = 2.0f * (float)M_PI * (float)phase / (float)pattern->period_ms;
        const float rise = (1.0f - cosf(turn)) * 0.5f;    /* 0 .. 1, smooth */
        const float span = 255.0f - (float)HK_LED_BREATHE_FLOOR;
        return (uint32_t)((float)HK_LED_BREATHE_FLOOR + span * rise);
    }
    /* Both blink speeds are a square wave; the pattern carries the period. */
    return (phase < (pattern->period_ms / 2u)) ? 255u : 0u;
}

/**
 * Convert one colour component to a duty value.
 *
 * The squaring step is gamma correction. Perceived brightness is roughly the
 * square root of emitted light, so a linear duty ramp reads as a bright flash
 * followed by nothing. Squaring makes a breathe look like a breathe.
 */
/** The component's level before gamma: 0-255, brightness already applied. */
static uint32_t level_for(uint8_t component, uint32_t env, uint8_t brightness)
{
    const uint32_t scaled = ((uint32_t)component * env) / 255u;
    return (scaled * brightness) / 100u;
}

static uint32_t duty_for(uint32_t level)
{
    return (level * level * HK_UI_DUTY_MAX) / (255u * 255u); /* gamma */
}

static void render(const hk_led_pattern_t *pattern, uint32_t time_ms)
{
    uint32_t env = envelope(pattern, time_ms);
    const uint8_t component[3] = {pattern->red, pattern->green, pattern->blue};
    uint32_t level[3];

    for (int i = 0; i < 3; i++) {
        level[i] = level_for(component[i], env, pattern->brightness);
        const uint32_t duty = duty_for(level[i]);
        /* Common cathode: a higher duty is brighter, and zero is off. */
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_CHANNELS[i].channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_CHANNELS[i].channel);
    }

#if CONFIG_HK_DEVKIT_STATUS_LED
    /* The same values, on the LED the devkit actually has.
     *
     * Taken from `duty` rather than recomputed from the pattern: a second
     * computation is a second thing to keep in step, and the whole point of a
     * mirror is that it cannot disagree with what it mirrors. The duties are
     * already gamma-corrected and brightness-scaled, so they only need to come
     * back down from the PWM range to the driver's 8 bits.
     *
     * Failures are ignored. This is a diagnostic surface on a bench board; a
     * board that cannot show its state is still a board that works, and taking
     * the UI task down over it would remove the LED and the button at once. */
    if (s_strip != NULL) {
        (void)led_strip_set_pixel(s_strip, 0, level[0], level[1], level[2]);
        (void)led_strip_refresh(s_strip);
    }
#endif
}

static void ui_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        uint32_t time_ms = now_ms();

        hk_button_event_t event = hk_button_update(&s_button, button_pressed(), time_ms);
        if (event != HK_BUTTON_EVENT_NONE && s_callback != NULL) {
            s_callback(event, s_context);
        }

        hk_led_inputs_t inputs;
        portENTER_CRITICAL(&s_status_lock);
        inputs = s_status;
        portEXIT_CRITICAL(&s_status_lock);
        /* The hold level belongs to this task; a caller cannot know it. */
        inputs.button_hold = hk_button_hold(&s_button, time_ms);

        render(hk_led_pattern(hk_led_resolve(&inputs)), time_ms);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HK_UI_POLL_MS));
    }
}

size_t hk_ui_stack_headroom(void)
{
    if (s_task == NULL) {
        return 0u;
    }
    /* uxTaskGetStackHighWaterMark counts stack words, not bytes. */
    return (size_t)uxTaskGetStackHighWaterMark(s_task) * sizeof(StackType_t);
}

esp_err_t hk_ui_start(hk_ui_event_cb_t callback, void *context)
{
    s_callback = callback;
    s_context = context;

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << HK_PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        /* The board also carries an external pull-up; the internal one keeps
         * the line defined if that resistor is not fitted. */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&button_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button gpio %d: %s", HK_PIN_BUTTON, esp_err_to_name(err));
        return err;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = HK_UI_DUTY_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = HK_UI_LED_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer at %d Hz: %s", HK_UI_LED_PWM_HZ, esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < 3; i++) {
        const ledc_channel_config_t channel = {
            .gpio_num = LED_CHANNELS[i].gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LED_CHANNELS[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc channel on gpio %d: %s",
                     LED_CHANNELS[i].gpio, esp_err_to_name(err));
            return err;
        }
    }

#if CONFIG_HK_DEVKIT_STATUS_LED
    start_onboard_mirror();
#endif

    /* Read the button before anything else so a boot-time hold is seen. */
    s_recovery = button_pressed();
    hk_button_init(&s_button, s_recovery, now_ms());

    memset(&s_status, 0, sizeof(s_status));
    s_faults = 0u;
    s_status.booting = true;

    BaseType_t created = xTaskCreate(ui_task, "hk_ui", HK_UI_TASK_STACK, NULL,
                                     HK_UI_TASK_PRIO, &s_task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "could not create the ui task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started: button gpio%d, rgb gpio%d/%d/%d at %d Hz%s",
             HK_PIN_BUTTON, HK_PIN_LED_R, HK_PIN_LED_G, HK_PIN_LED_B,
             HK_UI_LED_PWM_HZ, s_recovery ? ", button held at boot" : "");
    return ESP_OK;
}

void hk_ui_set_network(bool provisioning, bool connecting, bool ready)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.provisioning = provisioning;
    s_status.connecting = connecting;
    s_status.ready = ready;
    portEXIT_CRITICAL(&s_status_lock);
}

void hk_ui_set_fault(hk_ui_fault_t source, bool active)
{
    portENTER_CRITICAL(&s_status_lock);
    if (active) {
        s_faults |= (uint32_t)source;
    } else {
        s_faults &= ~(uint32_t)source;
    }
    /* Red while any source is still raised, so one subsystem recovering does
     * not clear another's fault. */
    s_status.error = (s_faults != 0u);
    portEXIT_CRITICAL(&s_status_lock);
}

void hk_ui_set_ota(bool active)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.ota = active;
    portEXIT_CRITICAL(&s_status_lock);
}

void hk_ui_set_playing(bool playing)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.playing = playing;
    portEXIT_CRITICAL(&s_status_lock);
}

void hk_ui_set_battery_low(bool low)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.battery_low = low;
    portEXIT_CRITICAL(&s_status_lock);
}

void hk_ui_clear_booting(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.booting = false;
    portEXIT_CRITICAL(&s_status_lock);
}

bool hk_ui_recovery_requested(void)
{
    return s_recovery;
}
