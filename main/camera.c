#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/jpeg_encode.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "camera.h"

#define BUFFER_COUNT 3
static const char *TAG = "camera";
static int camera_fd;
static uint8_t *frames[BUFFER_COUNT];
static size_t frame_lengths[BUFFER_COUNT];
static uint8_t *encoded, *latest;
static size_t encoded_capacity, latest_size;
static uint32_t latest_sequence;
static int64_t latest_capture_us;
static SemaphoreHandle_t latest_mutex;
static jpeg_encoder_handle_t encoder;
static esp_cam_sensor_xclk_handle_t xclk;
static uint32_t width, height;

static void capture_task(void *arg)
{
    int64_t last_encoded = 0;
    unsigned errors = 0;
    const jpeg_encode_cfg_t config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = CONFIG_CAMERA_STREAM_JPEG_QUALITY,
        .width = width, .height = height,
    };
    while (true) {
        struct v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                 .memory = V4L2_MEMORY_MMAP};
        if (ioctl(camera_fd, VIDIOC_DQBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Camera dequeue failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t captured = esp_timer_get_time();
        uint32_t size = 0;
        esp_err_t result = ESP_ERR_NOT_FINISHED;
        if (buf.index < BUFFER_COUNT &&
            captured - last_encoded >= 1000000 / CONFIG_CAMERA_STREAM_FPS) {
            last_encoded = captured;
            result = jpeg_encoder_process(encoder, &config, frames[buf.index],
                                          frame_lengths[buf.index], encoded,
                                          encoded_capacity, &size);
        }
        // Return the sensor buffer before any network client can block us.
        if (ioctl(camera_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Camera requeue failed: errno=%d", errno);
            abort();
        }
        if (result == ESP_OK && size <= CAMERA_JPEG_CAPACITY && size >= 4) {
            xSemaphoreTake(latest_mutex, portMAX_DELAY);
            memcpy(latest, encoded, size);
            latest_size = size;
            latest_capture_us = captured;
            latest_sequence++;
            uint32_t sequence = latest_sequence;
            xSemaphoreGive(latest_mutex);
            if (sequence == 1 || sequence % (CONFIG_CAMERA_STREAM_FPS * 10) == 0) {
                ESP_LOGI(TAG, "JPEG frame %" PRIu32 ": %" PRIu32 "x%" PRIu32 ", %" PRIu32 " bytes",
                         sequence, width, height, size);
            }
        } else if (result != ESP_ERR_NOT_FINISHED && ++errors % 10 == 1) {
            ESP_LOGW(TAG, "JPEG encode failed: %s (%" PRIu32 " bytes)", esp_err_to_name(result), size);
        }
    }
}

esp_err_t camera_start(void)
{
    // P4X-EYE: camera power=12, reset=26, XCLK=11, SCCB SCL/SDA=13/14.
    ESP_ERROR_CHECK(rtc_gpio_hold_dis(GPIO_NUM_12));
    ESP_ERROR_CHECK(rtc_gpio_deinit(GPIO_NUM_12));
    const gpio_config_t power = {.pin_bit_mask = 1ULL << 12, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&power));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_12, 1));
    vTaskDelay(pdMS_TO_TICKS(20));
    const esp_cam_sensor_xclk_config_t clock_config = {
        .esp_clock_router_cfg = {.xclk_pin = GPIO_NUM_11, .xclk_freq_hz = 24000000},
    };
    ESP_ERROR_CHECK(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk));
    ESP_ERROR_CHECK(esp_cam_sensor_xclk_start(xclk, &clock_config));
    const esp_video_init_csi_config_t csi = {
        .sccb_config = {.init_sccb = true,
                        .i2c_config = {.port = 0, .scl_pin = GPIO_NUM_13, .sda_pin = GPIO_NUM_14},
                        .freq = 100000},
        .reset_pin = GPIO_NUM_26,
        .pwdn_pin = -1, // GPIO12 is active-high board power enable, not sensor PWDN.
    };
    const esp_video_init_config_t init = {.csi = &csi};
    ESP_ERROR_CHECK(esp_video_init(&init));
    camera_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (camera_fd < 0) {
        ESP_LOGE(TAG, "open %s failed: errno=%d", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, errno);
        return ESP_FAIL;
    }
    struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if (ioctl(camera_fd, VIDIOC_G_FMT, &format) < 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: errno=%d", errno);
        return ESP_FAIL;
    }
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(camera_fd, VIDIOC_S_FMT, &format) < 0) {
            ESP_LOGE(TAG, "VIDIOC_S_FMT RGB565 failed: errno=%d", errno);
            return ESP_FAIL;
        }
    }
    width = format.fmt.pix.width;
    height = format.fmt.pix.height;
    ESP_LOGI(TAG, "OV2710 capture: %" PRIu32 "x%" PRIu32 " RGB565", width, height);
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 || width % 16 || height % 16) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    struct v4l2_requestbuffers request = {.count = BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(camera_fd, VIDIOC_REQBUFS, &request) < 0 || request.count < BUFFER_COUNT) return ESP_FAIL;
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        struct v4l2_buffer buf = {.index = i, .type = request.type, .memory = request.memory};
        if (ioctl(camera_fd, VIDIOC_QUERYBUF, &buf) < 0) return ESP_FAIL;
        frames[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera_fd, buf.m.offset);
        if (!frames[i] || frames[i] == (void *)-1) return ESP_ERR_NO_MEM;
        frame_lengths[i] = buf.length;
        if (ioctl(camera_fd, VIDIOC_QBUF, &buf) < 0) return ESP_FAIL;
    }
    const jpeg_encode_engine_cfg_t engine_config = {.timeout_ms = 1000};
    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&engine_config, &encoder));
    const jpeg_encode_memory_alloc_cfg_t memory = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    encoded = jpeg_alloc_encoder_mem(CAMERA_JPEG_CAPACITY, &memory, &encoded_capacity);
    latest = heap_caps_malloc(CAMERA_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    latest_mutex = xSemaphoreCreateMutex();
    if (!encoded || !latest || !latest_mutex) return ESP_ERR_NO_MEM;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd, VIDIOC_STREAMON, &type) < 0) return ESP_FAIL;
    if (xTaskCreate(capture_task, "camera_jpeg", 6144, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t camera_copy_jpeg(uint8_t *output, size_t capacity, size_t *size,
                           uint32_t *sequence, int64_t *captured_us)
{
    if (xSemaphoreTake(latest_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    // Do not serve a frozen image if camera acquisition stops.
    if (latest_size && latest_size <= capacity && esp_timer_get_time() - latest_capture_us < 2000000) {
        memcpy(output, latest, latest_size);
        *size = latest_size;
        *sequence = latest_sequence;
        *captured_us = latest_capture_us;
        result = ESP_OK;
    }
    xSemaphoreGive(latest_mutex);
    return result;
}
