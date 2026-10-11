#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "camera.h"
#include "network.h"

static uint8_t *http_jpeg;

static esp_err_t capture_handler(httpd_req_t *req)
{
    size_t size = 0;
    uint32_t sequence = 0;
    int64_t captured_us = 0;
    if (camera_copy_jpeg(http_jpeg, CAMERA_JPEG_CAPACITY, &size,
                         &sequence, &captured_us) != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Camera warming up; retry shortly.\n");
    }
    char seq[24], timestamp[32];
    snprintf(seq, sizeof(seq), "%" PRIu32, sequence);
    snprintf(timestamp, sizeof(timestamp), "%" PRId64, captured_us);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Sequence", seq);
    httpd_resp_set_hdr(req, "X-Capture-Us", timestamp);
    return httpd_resp_send(req, (const char *)http_jpeg, size);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<!doctype html><meta name='viewport' content='width=device-width'>"
        "<title>P4X-EYE camera</title><h1>ESP32-P4X-EYE</h1>"
        "<p>JPEG camera preview; ROS topic: /camera/image/compressed</p>"
        "<img id='camera' style='max-width:100%' alt='Camera warming up'>"
        "<script>const i=document.getElementById('camera');"
        "function next(){setTimeout(()=>i.src='/capture.jpg?t='+Date.now(),100);}"
        "i.onload=next;i.onerror=next;next();</script>");
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    if (chip.revision < 301) abort();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(camera_start());
    ESP_ERROR_CHECK(network_start());
    http_jpeg = heap_caps_malloc(CAMERA_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!http_jpeg) abort();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.send_wait_timeout = 3;
    config.recv_wait_timeout = 3;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    const httpd_uri_t capture = {.uri = "/capture.jpg", .method = HTTP_GET, .handler = capture_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &capture));
    ESP_LOGI("main", "Camera HTTP server started on port 80");
}
