idf_component_register(
    SRCS "../../main/bench_test.c" "../../main/pump_controller.c"
    INCLUDE_DIRS "../../main"
    REQUIRES driver esp_driver_usb_serial_jtag
)
