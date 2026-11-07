idf_component_register(
    SRCS "main.c" "ui.c" "audio.c" "papp_http.c" "net.c" "battery.c" "settings.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_wifi esp_netif nvs_flash esp_timer mbedtls esp_driver_i2c
    EMBED_FILES
        "assets/papp_cjk.ttf"
        "assets/demo/d1_src.wav" "assets/demo/d1_tgt.wav"
        "assets/demo/d2_src.wav" "assets/demo/d2_tgt.wav"
        "assets/demo/d3_src.wav" "assets/demo/d3_tgt.wav"
        "assets/demo/d4_src.wav" "assets/demo/d4_tgt.wav"
        "assets/demo/d5_src.wav" "assets/demo/d5_tgt.wav")
