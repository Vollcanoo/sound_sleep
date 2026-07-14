idf_component_register(
    SRCS "main.c" "wifi_manager.c" "cloud_llm_client.c" "pump_controller.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_wifi esp_http_client
             esp_event esp_netif json esp_tls
)
