/* Status LED: plain GPIO LED or addressable WS2812/SK6812. */
#pragma once

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_PATTERN_OFF = 0,      /* trigger idle (switch/pulse mode, inactive)    */
    LED_PATTERN_PAIRING,      /* fast blink   - waiting for a new phone        */
    LED_PATTERN_SEARCHING,    /* slow blink   - looking for the paired phone   */
    LED_PATTERN_CONNECTED,    /* solid on     - connected, automation fired     */
    LED_PATTERN_RESET,        /* very fast    - forgetting phones               */
} led_pattern_t;

void led_init(const btt_config_t *cfg);
void led_set_pattern(led_pattern_t p);
/* Call every 50 ms from the main loop. */
void led_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
