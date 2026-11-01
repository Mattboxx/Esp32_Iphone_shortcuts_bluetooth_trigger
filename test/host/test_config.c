/* Host test: runs the REAL firmware parser (src/config.c) on a blob
 * produced by the web flasher (docs/js/config.js) and prints the result
 * as JSON, so run_tests.mjs can compare both sides. */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_partition.h"

static unsigned char g_blob[64];
static int g_have_part = 1;
static esp_partition_t g_part;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t t, esp_partition_subtype_t s, const char *l)
{
    (void)t; (void)s; (void)l;
    return g_have_part ? &g_part : NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t n)
{
    (void)p;
    if (off + n > sizeof(g_blob)) return ESP_FAIL;
    memcpy(dst, g_blob + off, n);
    return ESP_OK;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    size_t got = fread(g_blob, 1, sizeof(g_blob), f);
    fclose(f);
    if (got != sizeof(g_blob)) memset(g_blob + got, 0xFF, sizeof(g_blob) - got);

    btt_config_t c;
    btt_config_load(&c);
    printf("{\"name\":\"%s\",\"reconnectS\":%u,\"pulseS\":%u,\"pairWindowS\":%u,"
           "\"ledGpio\":%d,\"ledType\":%u,\"ledActiveLow\":%s,\"ledBrightness\":%u,"
           "\"buttonGpio\":%d,\"triggerMode\":%u,\"triggerGpio\":%d,\"triggerActiveHigh\":%s,"
           "\"fromFlash\":%s}\n",
           c.name, c.reconnect_s, c.pulse_s, c.pair_window_s, c.led_gpio, c.led_type,
           c.led_active_low ? "true" : "false", c.led_brightness, c.button_gpio,
           c.trigger_mode, c.trigger_gpio, c.trigger_active_high ? "true" : "false",
           c.from_flash ? "true" : "false");
    return 0;
}
