/*
 * ============================================================
 *   ESP32 BT TRIGGER  -  turn "power on" into an iPhone Shortcut
 * ============================================================
 *
 *   The board shows up on the iPhone as a Bluetooth MOUSE that never moves.
 *   The act of CONNECTING is the trigger: iOS runs a Shortcuts automation
 *   set to "When <device> connects" (and optionally "disconnects").
 *
 *   - original ESP32         -> Classic Bluetooth HID (transport_classic.c)
 *   - ESP32-S3 / C3 / C6     -> Bluetooth Low Energy HID (transport_ble.c)
 *
 *   Trigger modes (set them in the web flasher, no code needed):
 *   - always : connect whenever powered (power it from a car, scooter, lamp...)
 *   - switch : connected while an input pin is active (reed switch, relay...)
 *   - pulse  : a button press connects for N seconds, then disconnects
 *
 *   Settings are read from the "btcfg" flash partition (see config.c).
 * ============================================================
 */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "led.h"
#include "transport.h"

static const char *TAG = "BTTRIGGER";

#define LOOP_MS          50
#define FORGET_HOLD_MS   5000
#define DEBOUNCE_SAMPLES 3

static btt_config_t s_cfg;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void input_init(int gpio, bool active_high)
{
    if (gpio < 0) {
        return;
    }
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    /* Input-only pads (GPIO34-39 on ESP32) have no pulls: the call just fails. */
    gpio_set_pull_mode(gpio, active_high ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
}

static bool input_active(int gpio, bool active_high)
{
    return gpio >= 0 && (gpio_get_level(gpio) == (active_high ? 1 : 0));
}

/* ---- "hold 5 s to forget all phones" button ---- */
static void button_task_step(uint32_t t)
{
    static bool     seen_released = false;   /* never act on a pin stuck low at boot */
    static uint32_t pressed_at    = 0;

    if (s_cfg.button_gpio < 0) {
        return;
    }
    bool pressed = input_active(s_cfg.button_gpio, false);
    if (!pressed) {
        seen_released = true;
        pressed_at = 0;
        return;
    }
    if (!seen_released) {
        return;
    }
    if (pressed_at == 0) {
        pressed_at = t ? t : 1;
    } else if (t - pressed_at >= FORGET_HOLD_MS) {
        ESP_LOGW(TAG, "Button held 5 s: forgetting all paired phones and restarting");
        led_set_pattern(LED_PATTERN_RESET);
        for (int i = 0; i < 20; i++) {
            led_tick(now_ms());
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
        }
        transport_forget_all();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

/* ---- trigger input -> link enabled? ---- */
static bool trigger_step(uint32_t t)
{
    static int      stable = 0;
    static bool     level  = false;      /* debounced input level */
    static bool     prev   = false;
    static uint32_t pulse_until = 0;

    if (s_cfg.trigger_mode == TRIGGER_ALWAYS) {
        return true;
    }

    bool raw = input_active(s_cfg.trigger_gpio, s_cfg.trigger_active_high);
    if (raw != level) {
        if (++stable >= DEBOUNCE_SAMPLES) {
            level = raw;
            stable = 0;
        }
    } else {
        stable = 0;
    }

    bool rising = level && !prev;
    prev = level;

    if (s_cfg.trigger_mode == TRIGGER_SWITCH) {
        return level;
    }

    /* PULSE: each press (re)starts the window */
    if (rising) {
        ESP_LOGI(TAG, "Button pressed: connecting for %u s", s_cfg.pulse_s);
        pulse_until = t + (uint32_t)s_cfg.pulse_s * 1000u;
        if (pulse_until == 0) {
            pulse_until = 1;
        }
    }
    if (pulse_until && (int32_t)(t - pulse_until) >= 0) {
        pulse_until = 0;
    }
    return pulse_until != 0;
}

/* The link is up when the trigger says so. Exception: while no phone is
 * paired yet the device stays visible, otherwise a pin/button trigger that
 * is inactive would make the very first pairing impossible. */
static bool link_wanted(uint32_t t)
{
    static bool announced = false;
    bool trig = trigger_step(t);
    if (trig || s_cfg.trigger_mode == TRIGGER_ALWAYS) {
        return true;
    }
    if (!transport_has_bonds()) {
        if (!announced) {
            ESP_LOGI(TAG, "No phone paired yet: visible for pairing, then the pin takes over");
            announced = true;
        }
        return true;
    }
    return false;
}

static led_pattern_t pattern_for(link_state_t st)
{
    switch (st) {
        case LINK_CONNECTED: return LED_PATTERN_CONNECTED;
        case LINK_PAIRING:   return LED_PATTERN_PAIRING;
        case LINK_SEARCHING: return LED_PATTERN_SEARCHING;
        default:             return LED_PATTERN_OFF;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    btt_config_load(&s_cfg);
    ESP_LOGI(TAG, "ESP32 BT Trigger v%s (%s edition) - %s", BTT_FW_VERSION, BTT_EDITION_NAME,
#if CONFIG_BT_CLASSIC_ENABLED
             "Classic Bluetooth HID"
#else
             "Bluetooth LE HID"
#endif
    );
    btt_config_log(&s_cfg);

    led_init(&s_cfg);
    input_init(s_cfg.button_gpio, false);
    if (s_cfg.trigger_gpio != s_cfg.button_gpio) {
        input_init(s_cfg.trigger_gpio, s_cfg.trigger_active_high);
    }

    transport_init(&s_cfg);
    transport_set_enabled(link_wanted(now_ms()));

    ESP_LOGI(TAG, "Ready. On the iPhone: Settings > Bluetooth > '%s'", s_cfg.name);

    uint32_t last_sec = now_ms();
    link_state_t last_state = (link_state_t)-1;

    while (true) {
        uint32_t t = now_ms();

        transport_set_enabled(link_wanted(t));

        /* Pairing window: new phones can pair only during the first N s,
         * or at any time while no phone is paired yet. */
        bool pairable = s_cfg.pair_window_s == 0 ||
                        t < (uint32_t)s_cfg.pair_window_s * 1000u ||
                        transport_state() == LINK_PAIRING;
        transport_set_pairable(pairable);

        button_task_step(t);

        if (t - last_sec >= 1000) {
            last_sec += 1000;
            transport_tick_1s();
        }

        link_state_t st = transport_state();
        if (st != last_state) {
            static const char *names[] = {"idle", "pairing", "searching", "connected"};
            ESP_LOGI(TAG, "State: %s", names[st]);
            last_state = st;
        }
        led_set_pattern(pattern_for(st));
        led_tick(t);

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}
