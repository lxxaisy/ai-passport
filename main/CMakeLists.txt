idf_component_register(
    SRCS "am_main.c"
         "am_model.c"
         "am_protocol.c"
         "am_transport.c"
         "am_view.c"
         "../assets/fonts/am_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer esp_system esp_app_format json nvs_flash esp_driver_usb_serial_jtag
)
