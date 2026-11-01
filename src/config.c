#include "config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

static const char *TAG = "CONFIG";

#define CFG_PART_TYPE    0x40
#define CFG_PART_SUBTYPE 0x00
#define CFG_PART_LABEL   "btcfg"

/* Default pins: BOOT button on GPIO0, blue LED on GPIO2 (most ESP32 DevKit / D1 mini32). */
#define DEFAULT_BUTTON_GPIO 0
#define DEFAULT_LED_GPIO    2
#define DEFAULT_LED_TYPE    LED_PLAIN

static void set_defaults(btt_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->name, "ESP32-BT-Trigger");
    c->reconnect_s         = 8;
    c->pulse_s             = 10;
    c->pair_window_s       = 0;
    c->led_gpio            = DEFAULT_LED_GPIO;
    c->led_type            = DEFAULT_LED_TYPE;
    c->led_active_low      = false;
    c->led_brightness      = 32;
    c->button_gpio         = DEFAULT_BUTTON_GPIO;
    c->trigger_mode        = TRIGGER_ALWAYS;
    c->trigger_gpio        = -1;
    c->trigger_active_high = false;
    c->trigger_pull        = PULL_AUTO;
    c->debounce_ms         = 50;
    c->hold_s              = 0;
    c->tx_power_set        = false;
    c->tx_power_dbm        = 0;
    c->from_flash          = false;
}

/* Standard CRC-32 (zlib / IEEE 802.3), same as the web flasher. */
static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Pins wired to the SPI flash: touching them crashes the chip. */
static bool pin_reserved(int g)
{
    return g >= 6 && g <= 11;   /* ESP32: GPIO6-11 drive the SPI flash */
}

static int8_t sane_gpio(int8_t g, bool output)
{
    if (g < 0) {
        return -1;
    }
    if (pin_reserved(g) || (output ? !GPIO_IS_VALID_OUTPUT_GPIO(g) : !GPIO_IS_VALID_GPIO(g))) {
        ESP_LOGW(TAG, "GPIO %d is not usable here, ignoring it", g);
        return -1;
    }
    return g;
}

void btt_config_load(btt_config_t *c)
{
    set_defaults(c);

    const esp_partition_t *part = esp_partition_find_first(
        (esp_partition_type_t)CFG_PART_TYPE, (esp_partition_subtype_t)CFG_PART_SUBTYPE, CFG_PART_LABEL);
    if (!part) {
        ESP_LOGW(TAG, "No '%s' partition: using defaults", CFG_PART_LABEL);
        return;
    }

    uint8_t b[BTT_CFG_SIZE];
    if (esp_partition_read(part, 0, b, sizeof(b)) != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read config: using defaults");
        return;
    }
    if (rd32(b) != BTT_CFG_MAGIC) {
        ESP_LOGI(TAG, "No saved settings: using defaults");
        return;
    }
    if (b[4] != BTT_CFG_VERSION || b[5] != BTT_CFG_SIZE) {
        ESP_LOGW(TAG, "Unknown settings version %u: using defaults", b[4]);
        return;
    }
    if (crc32_ieee(b, BTT_CFG_SIZE - 4) != rd32(b + BTT_CFG_SIZE - 4)) {
        ESP_LOGW(TAG, "Settings CRC mismatch: using defaults");
        return;
    }

    /* Layout (offsets) - must match docs/js/config.js */
    char name[BTT_NAME_MAX + 1];
    memcpy(name, b + 8, BTT_NAME_MAX);
    name[BTT_NAME_MAX] = '\0';
    if (name[0] != '\0') {
        strcpy(c->name, name);
    }
    c->reconnect_s         = rd16(b + 40);
    c->pulse_s             = rd16(b + 42);
    c->pair_window_s       = rd16(b + 44);
    c->led_gpio            = (int8_t)b[46];
    c->led_type            = b[47];
    c->led_active_low      = b[48] != 0;
    c->led_brightness      = b[49];
    c->button_gpio         = (int8_t)b[50];
    c->trigger_mode        = b[51];
    c->trigger_gpio        = (int8_t)b[52];
    c->trigger_active_high = b[53] != 0;
    /* Bytes 54..59 were reserved (zero) in early v2 settings: zero = default. */
    c->trigger_pull        = b[54];
    c->debounce_ms         = rd16(b + 56) ? rd16(b + 56) : 50;
    c->hold_s              = rd16(b + 58);
    /* Byte 55: 0 = default TX power, otherwise dBm + 100 (76..121 = -24..+21). */
    c->tx_power_set        = b[55] != 0;
    c->tx_power_dbm        = (int8_t)((int)b[55] - 100);
    c->from_flash          = true;

    /* Sanitize everything: a bad value must never brick the trigger. */
    if (c->reconnect_s < 3)  c->reconnect_s = 3;
    if (c->reconnect_s > 600) c->reconnect_s = 600;
    if (c->pulse_s < 1)      c->pulse_s = 1;
    if (c->led_type > LED_WS2812) c->led_type = LED_NONE;
    if (c->led_brightness == 0) c->led_brightness = 32;
    if (c->trigger_mode > TRIGGER_PULSE) c->trigger_mode = TRIGGER_ALWAYS;
    if (c->trigger_pull > PULL_NONE) c->trigger_pull = PULL_AUTO;
    if (c->debounce_ms < 10)   c->debounce_ms = 10;
    if (c->debounce_ms > 5000) c->debounce_ms = 5000;
    if (c->hold_s > 3600)      c->hold_s = 3600;
    if (c->tx_power_set) {
        if (c->tx_power_dbm < -24) c->tx_power_dbm = -24;
        if (c->tx_power_dbm > 21)  c->tx_power_dbm = 21;
    }
    c->led_gpio     = sane_gpio(c->led_gpio, true);
    c->button_gpio  = sane_gpio(c->button_gpio, false);
    c->trigger_gpio = sane_gpio(c->trigger_gpio, false);
    if (c->led_gpio < 0) c->led_type = LED_NONE;
    if (c->trigger_mode != TRIGGER_ALWAYS && c->trigger_gpio < 0) {
        ESP_LOGW(TAG, "Trigger mode needs an input pin: falling back to 'always'");
        c->trigger_mode = TRIGGER_ALWAYS;
    }
}

void btt_config_log(const btt_config_t *c)
{
    static const char *modes[] = {"always (power-on)", "switch", "pulse (button)"};
    static const char *leds[]  = {"none", "plain", "WS2812"};
    ESP_LOGI(TAG, "Settings (%s):", c->from_flash ? "from web flasher" : "defaults");
    ESP_LOGI(TAG, "  name          : %s", c->name);
    static const char *pulls[] = {"auto", "pull-up", "pull-down", "none (external)"};
    ESP_LOGI(TAG, "  trigger mode  : %s", modes[c->trigger_mode]);
    if (c->trigger_mode != TRIGGER_ALWAYS) {
        ESP_LOGI(TAG, "  input         : GPIO %d, active %s, resistor %s, debounce %u ms",
                 c->trigger_gpio, c->trigger_active_high ? "HIGH" : "LOW",
                 pulls[c->trigger_pull], c->debounce_ms);
    }
    if (c->trigger_mode == TRIGGER_PULSE) {
        ESP_LOGI(TAG, "  pulse         : %u s", c->pulse_s);
    }
    if (c->trigger_mode == TRIGGER_SWITCH && c->hold_s) {
        ESP_LOGI(TAG, "  release delay : %u s", c->hold_s);
    }
    ESP_LOGI(TAG, "  status LED    : %s on GPIO %d%s", leds[c->led_type], c->led_gpio,
             c->led_active_low ? " (active LOW)" : "");
    ESP_LOGI(TAG, "  reset button  : GPIO %d (hold 5 s = forget phones)", c->button_gpio);
    ESP_LOGI(TAG, "  pairing       : %s", c->pair_window_s ? "window after boot" : "always open");
    if (c->pair_window_s) {
        ESP_LOGI(TAG, "  pair window   : %u s", c->pair_window_s);
    }
    ESP_LOGI(TAG, "  reconnect     : every %u s", c->reconnect_s);
    if (c->tx_power_set) {
        ESP_LOGI(TAG, "  TX power      : %d dBm (nearest level supported)", c->tx_power_dbm);
    } else {
        ESP_LOGI(TAG, "  TX power      : default");
    }
}

int btt_nearest_level(const int8_t *levels, int n, int dbm)
{
    int best = 0;
    for (int i = 1; i < n; i++) {
        int d  = levels[i] - dbm;
        int db = levels[best] - dbm;
        if ((d < 0 ? -d : d) < (db < 0 ? -db : db)) {
            best = i;
        }
    }
    return best;
}
