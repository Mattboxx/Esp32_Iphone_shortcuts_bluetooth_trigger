#include "led.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "LED";

static led_type_t    s_type = LED_NONE;
static int           s_gpio = -1;
static bool          s_active_low;
static uint8_t       s_bright = 32;
static led_pattern_t s_pattern = LED_PATTERN_OFF;
static int           s_last = -1;   /* last state written, avoids useless writes */

static rmt_channel_handle_t s_rmt;
static rmt_encoder_handle_t s_enc;

static bool ws2812_init(int gpio)
{
    rmt_tx_channel_config_t ch = {
        .gpio_num          = gpio,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,      /* 0.1 us per tick */
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 2,
    };
    if (rmt_new_tx_channel(&ch, &s_rmt) != ESP_OK) {
        return false;
    }
    rmt_bytes_encoder_config_t enc = {
        .bit0 = {.duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0},
        .bit1 = {.duration0 = 9, .level0 = 1, .duration1 = 3, .level1 = 0},
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&enc, &s_enc) != ESP_OK) {
        return false;
    }
    return rmt_enable(s_rmt) == ESP_OK;
}

void led_init(const btt_config_t *cfg)
{
    s_type       = (led_type_t)cfg->led_type;
    s_gpio       = cfg->led_gpio;
    s_active_low = cfg->led_active_low;
    s_bright     = cfg->led_brightness;
    if (s_gpio < 0 || s_type == LED_NONE) {
        s_type = LED_NONE;
        return;
    }
    if (s_type == LED_PLAIN) {
        gpio_reset_pin(s_gpio);
        gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(s_gpio, s_active_low ? 1 : 0);
    } else if (!ws2812_init(s_gpio)) {
        ESP_LOGW(TAG, "WS2812 init failed on GPIO %d, LED disabled", s_gpio);
        s_type = LED_NONE;
    }
}

void led_set_pattern(led_pattern_t p)
{
    s_pattern = p;
}

/* Colors per pattern for RGB LEDs (R, G, B at full scale). */
static void pattern_color(led_pattern_t p, uint8_t rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0;
    switch (p) {
        case LED_PATTERN_PAIRING:   rgb[2] = 255; break;               /* blue   */
        case LED_PATTERN_SEARCHING: rgb[0] = 255; rgb[1] = 120; break; /* amber  */
        case LED_PATTERN_CONNECTED: rgb[1] = 255; break;               /* green  */
        case LED_PATTERN_RESET:     rgb[0] = 255; break;               /* red    */
        default: break;
    }
}

static void write_led(bool on)
{
    int state = on ? (int)s_pattern + 1 : 0;
    if (state == s_last) {
        return;
    }
    s_last = state;

    if (s_type == LED_PLAIN) {
        gpio_set_level(s_gpio, (on ^ s_active_low) ? 1 : 0);
    } else if (s_type == LED_WS2812) {
        uint8_t rgb[3];
        pattern_color(on ? s_pattern : LED_PATTERN_OFF, rgb);
        uint8_t grb[3] = {
            (uint8_t)(rgb[1] * s_bright / 255),
            (uint8_t)(rgb[0] * s_bright / 255),
            (uint8_t)(rgb[2] * s_bright / 255),
        };
        rmt_transmit_config_t tx = {.loop_count = 0};
        rmt_transmit(s_rmt, s_enc, grb, sizeof(grb), &tx);
        rmt_tx_wait_all_done(s_rmt, 20);
    }
}

void led_tick(uint32_t now_ms)
{
    if (s_type == LED_NONE) {
        return;
    }
    bool on = false;
    switch (s_pattern) {
        case LED_PATTERN_OFF:       on = false; break;
        case LED_PATTERN_PAIRING:   on = (now_ms / 150) % 2; break;
        case LED_PATTERN_SEARCHING: on = (now_ms % 1500) < 150; break;
        case LED_PATTERN_CONNECTED: on = true; break;
        case LED_PATTERN_RESET:     on = (now_ms / 50) % 2; break;
    }
    write_led(on);
}
