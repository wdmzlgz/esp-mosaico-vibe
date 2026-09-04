idf_component_register(
    SRCS "main.c" "shooter_game.c" "game_audio.c" "raylib_screen_mirror.c"
         "../../factory/main/iris_ota_support.c"
    INCLUDE_DIRS "." "../../factory/main"
    REQUIRES esp-gsp esp_iris esp-mosaico-bsp esp_timer mosaico_game
             mosaico_game_input mosaico_game_assets mosaico_game_debug
             mosaico_raylib_fast mosaico_raylib_port nvs_flash raylib)

set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas_480.png")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py" "${canvas_png}"
    RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate Raylib Canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/scene.json.in"
               "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)
