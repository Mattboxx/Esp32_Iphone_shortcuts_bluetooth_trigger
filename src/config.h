/*
 * Runtime configuration.
 *
 * The settings live in a tiny dedicated flash partition ("btcfg", 4 KB at
 * 0x180000, see partitions.csv) so that the web flasher can write them
 * without recompiling or patching the firmware image. If the partition is
 * blank or invalid, the built-in defaults below are used, so a plain
 * firmware flash still works out of the box.
 *
 * The binary layout is a contract with docs/js/config.js. Keep them in sync.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTT_CFG_MAGIC        0x47545442u   /* "BTTG" little-endian */
#define BTT_CFG_VERSION      1
#define BTT_CFG_SIZE         64
#define BTT_NAME_MAX         29            /* bytes (fixed by the settings format) */

/* Firmware version shown in the logs and in the web flasher. */
#define BTT_FW_VERSION       "2.1.0"

typedef enum {
    TRIGGER_ALWAYS = 0,   /* connect whenever powered (classic use)         */
    TRIGGER_SWITCH = 1,   /* connected while an input pin is active         */
    TRIGGER_PULSE  = 2,   /* button press -> connect for N seconds          */
} trigger_mode_t;

typedef enum {
    PULL_AUTO = 0,        /* pull-up if active LOW, pull-down if active HIGH */
    PULL_UP   = 1,
    PULL_DOWN = 2,
    PULL_NONE = 3,        /* floating: external resistor or push-pull source */
} pull_mode_t;

typedef enum {
    LED_NONE   = 0,
    LED_PLAIN  = 1,       /* ordinary LED on a GPIO                         */
    LED_WS2812 = 2,       /* addressable RGB LED (WS2812 / SK6812)          */
} led_type_t;

typedef struct {
    char     name[BTT_NAME_MAX + 1];
    uint16_t reconnect_s;      /* Classic BT: seconds between reconnect attempts */
    uint16_t pulse_s;          /* PULSE mode: seconds to stay connected          */
    uint16_t pair_window_s;    /* 0 = always pairable, else window after boot     */
    int8_t   led_gpio;         /* -1 = disabled                                   */
    uint8_t  led_type;         /* led_type_t                                      */
    bool     led_active_low;
    uint8_t  led_brightness;   /* WS2812 only, 1..255                             */
    int8_t   button_gpio;      /* hold 5 s = forget all phones. -1 = disabled     */
    uint8_t  trigger_mode;     /* trigger_mode_t                                  */
    int8_t   trigger_gpio;     /* SWITCH / PULSE input                            */
    bool     trigger_active_high;
    uint8_t  trigger_pull;     /* pull_mode_t                                     */
    uint16_t debounce_ms;      /* input must be stable this long (10..5000)      */
    uint16_t hold_s;           /* SWITCH: stay connected N s after release       */
    bool     tx_power_set;     /* false = keep Espressif's default TX power      */
    int8_t   tx_power_dbm;     /* wanted TX power; nearest level the chip has    */
    bool     from_flash;       /* true if loaded from the btcfg partition         */
} btt_config_t;

/* Index of the entry of levels[] closest to dbm (TX power tables). */
int btt_nearest_level(const int8_t *levels, int n, int dbm);

/* Load the configuration (defaults if the partition is blank/invalid). */
void btt_config_load(btt_config_t *cfg);

/* Print the active configuration to the log. */
void btt_config_log(const btt_config_t *cfg);

#ifdef __cplusplus
}
#endif
