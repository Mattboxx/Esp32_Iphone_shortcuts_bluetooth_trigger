#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;
typedef struct { int dummy; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char *);
esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
