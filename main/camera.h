#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define CAMERA_JPEG_CAPACITY (1024 * 1024)
esp_err_t camera_start(void);
esp_err_t camera_copy_jpeg(uint8_t *output, size_t capacity, size_t *size,
                           uint32_t *sequence, int64_t *captured_us);
