idf_component_register(
    SRCS
        "main.c"
        "board_display.c"
        "../app/gsp_air_battle_app.c"
    INCLUDE_DIRS
        "."
        "../app"
    REQUIRES
        esp_iris
        esp-mosaico-bsp
        esp_mosaico_app_recovery
        freertos
        nvs_flash
    PRIV_REQUIRES
        esp-gsp
        esp_lcd
)

gsp_add_bundle(${COMPONENT_LIB}
    SCENES "../ui/main.json"
    PIXEL_FORMAT rgb565)
