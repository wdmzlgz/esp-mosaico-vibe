idf_component_register(
    SRCS "mosaico_raylib_port.c"
    INCLUDE_DIRS "include"
    REQUIRES esp-gsp raylib mosaico_game esp_timer heap
)
