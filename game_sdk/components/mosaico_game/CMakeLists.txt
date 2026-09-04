idf_component_register(
    SRCS "mosaico_game.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_system heap esp_iris esp_partition mbedtls spi_flash
)
