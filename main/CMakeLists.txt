idf_component_register(SRCS "main.c" "camera.c" "network.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_video esp_cam_sensor esp_wifi_remote
                                     esp_wifi esp_netif esp_event nvs_flash
                                     esp_http_server esp_driver_jpeg esp_driver_gpio
                                     esp_driver_i2c esp_timer)
