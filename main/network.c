#include <string.h>
#include "driver/rtc_io.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "network.h"
#include "wifi_credentials.h"

static const char *TAG = "network";

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u), reconnecting", event->reason);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) ESP_LOGW(TAG, "Reconnect: %s", esp_err_to_name(err));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "CAMERA_URL=http://" IPSTR "/capture.jpg", IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Browser preview: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

esp_err_t network_start(void)
{
    _Static_assert(sizeof(CAMERA_WIFI_SSID) <= 33, "Wi-Fi SSID exceeds 32 bytes");
    _Static_assert(sizeof(CAMERA_WIFI_PASSWORD) <= 64, "Wi-Fi password exceeds 63 bytes");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, "p4x-eye"));
    ESP_ERROR_CHECK(rtc_gpio_hold_dis(GPIO_NUM_9));
    ESP_ERROR_CHECK(rtc_gpio_deinit(GPIO_NUM_9));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, CAMERA_WIFI_SSID, sizeof(CAMERA_WIFI_SSID) - 1);
    memcpy(config.sta.password, CAMERA_WIFI_PASSWORD, sizeof(CAMERA_WIFI_PASSWORD) - 1);
    config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_LOGI(TAG, "Connecting to configured Wi-Fi using onboard ESP32-C6");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return ESP_OK;
}
