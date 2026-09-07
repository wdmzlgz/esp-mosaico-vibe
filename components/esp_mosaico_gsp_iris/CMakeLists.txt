idf_component_register(
    SRCS "iris_gsp_debug.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_iris
        esp_lcd
        esp-gsp
        freertos
)
