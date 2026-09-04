idf_component_register(
    SRCS "main.c" "tower_game.c" "tower_audio.c" "tower_screen_mirror.c"
         "../../factory/main/iris_ota_support.c"
    INCLUDE_DIRS "." "../../factory/main"
    REQUIRES esp-gsp esp_iris esp-mosaico-bsp esp_timer mosaico_game
             mosaico_game_input mosaico_game_assets mosaico_game_debug
             mosaico_game_2d mosaico_game_tilemap mosaico_game_audio
             mosaico_raylib_fast mosaico_raylib_port nvs_flash raylib)

set(GAME_ASSET_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../assets_src")
set(GAME_ASSET_OUTPUT "${CMAKE_CURRENT_LIST_DIR}/../assets/generated")
add_custom_target(tower_game_assets ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../../game_sdk/tools/pack_game_assets.py"
            --source "${GAME_ASSET_SOURCE}" --output "${GAME_ASSET_OUTPUT}"
    BYPRODUCTS "${GAME_ASSET_OUTPUT}/tower.atlas"
               "${GAME_ASSET_OUTPUT}/terrain.atlas"
               "${GAME_ASSET_OUTPUT}/level01.map"
    DEPENDS "${GAME_ASSET_SOURCE}/game_assets.json"
            "${GAME_ASSET_SOURCE}/atlas.json"
            "${GAME_ASSET_SOURCE}/level01.tmj"
            "${GAME_ASSET_SOURCE}/tower_atlas_source.png")
add_dependencies(${COMPONENT_LIB} tower_game_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${GAME_ASSET_OUTPUT}")
spiffs_create_partition_assets(
    game_assets "${GAME_ASSET_OUTPUT}"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".atlas,.map,.sound"
    MMAP_FILE_ALIGNMENT 64
    IMPORT_INC_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS tower_game_assets)

set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas_480.png")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py" "${canvas_png}"
    RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate Tower Defense Canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/scene.json.in"
               "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)
