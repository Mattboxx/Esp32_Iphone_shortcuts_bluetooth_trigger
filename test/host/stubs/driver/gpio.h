#pragma once
/* ESP32 rules: 0-39 exist except 20, 24, 28-31; 34-39 are input-only. */
#define GPIO_IS_VALID_GPIO(g) ((g) >= 0 && (g) <= 39 && (g) != 20 && (g) != 24 && !((g) >= 28 && (g) <= 31))
#define GPIO_IS_VALID_OUTPUT_GPIO(g) (GPIO_IS_VALID_GPIO(g) && (g) < 34)
