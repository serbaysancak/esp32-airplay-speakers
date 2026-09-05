# esp_hw_support provides esp_read_mac(), spi_flash provides esp_flash_get_physical_size(), esp_psram the PSRAM size,
# app_update provides the running-partition lookup. They are listed explicitly
# rather than relied on as transitive requirements.
idf_component_register(SRCS "hk_main.c"
                       INCLUDE_DIRS ""
                       REQUIRES hk_airplay
                                hk_button
                                hk_identity
                                hk_led
                                hk_pins
                                hk_provision
                                hk_storage
                                hk_network
                                hk_ui
                                hk_version
                                hk_ota
                                hk_gate
                                hk_health
                                hk_power
                                hk_audio
                                hk_sched
                                hk_settings
                                app_update
                                esp_system
                                esp_hw_support
                                spi_flash
                                esp_psram)
