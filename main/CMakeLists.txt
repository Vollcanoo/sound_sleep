set(base_sources
    "main.c" "wifi_manager.c" "cloud_llm_client.c" "cloud_upload.c"
    "pump_controller.c" "pump_rules.c" "ble_uart_server.c" "sleep_session.c"
    "wifi_provision.c" "posture_sensor.c")
set(snore_model "${CMAKE_CURRENT_LIST_DIR}/models/snoring_esp32_int8.espdl")

if(EXISTS "${snore_model}")
    set(snore_sources "snore_detector.cpp" "audio_capture.cpp" "log_mel.cpp")
    set(snore_requires espressif__esp-dl espressif__esp-dsp)
else()
    message(WARNING "Snore model is missing; building posture-only firmware. Add main/models/snoring_esp32_int8.espdl to enable snore inference.")
    set(snore_sources "snore_detector_stub.c")
    set(snore_requires)
endif()

idf_component_register(
    SRCS ${base_sources} ${snore_sources}
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc nvs_flash esp_wifi esp_http_client
             esp_event esp_netif json esp-tls bt ${snore_requires}
)

if(EXISTS "${snore_model}")
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
    include(${espdl_dir}/fbs_loader/cmake/utilities.cmake)
    target_add_aligned_binary_data(${COMPONENT_LIB} ${snore_model} BINARY)
endif()
