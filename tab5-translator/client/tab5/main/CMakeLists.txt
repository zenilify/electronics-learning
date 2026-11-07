idf_component_register(
    SRCS "main.c" "ui.c" "audio.c" "papp_http.c" "net.c" "battery.c" "settings.c" "p4_rev1_ledc.c" "c6ota.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ledc esp_driver_gpio esp_http_client esp_wifi esp_netif nvs_flash esp_timer mbedtls esp_driver_i2c
    EMBED_FILES
        "assets/papp_cjk.ttf"
        "assets/demo/d1_src.wav" "assets/demo/d1_tgt.wav"
        "assets/demo/d2_src.wav" "assets/demo/d2_tgt.wav"
        "assets/demo/d3_src.wav" "assets/demo/d3_tgt.wav"
        "assets/demo/d4_src.wav" "assets/demo/d4_tgt.wav"
        "assets/demo/d5_src.wav" "assets/demo/d5_tgt.wav")

# P4 rev<3 LEDC workaround (backlight as plain GPIO), see p4_rev1_ledc.c
foreach(fn ledc_timer_config ledc_channel_config ledc_set_duty ledc_update_duty)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}" "-Wl,-u,__wrap_${fn}")
endforeach()
