# SPDX-License-Identifier: Apache-2.0
#
# Emscripten host: link portable C against official espressif/esp-gsp 1.0.0
# from the Component Registry (fetched by tools/gsp-sim/fetch_gspc.py --sdk).

cmake_minimum_required(VERSION 3.16)
project(gsp_app_sim LANGUAGES C)

set(GSP_SIM_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(REPO_ROOT "${GSP_SIM_ROOT}/../..")
set(ESP_GSP_ROOT "${GSP_SIM_ROOT}/sdk/esp-gsp" CACHE PATH
    "Official espressif/esp-gsp 1.0.0 Component Registry package")
set(GSP_APP_DIR "${REPO_ROOT}/projects/gsp_hello/app" CACHE PATH
    "Portable application sources that export gsp_app_start()")
set(GSP_APP_BUNDLE "${GSP_SIM_ROOT}/build-web/scene/preview.gspb" CACHE FILEPATH
    "Packed .gspb preloaded into the WASM virtual filesystem")

include("${ESP_GSP_ROOT}/cmake/gsp_sim_prebuilt.cmake")
gsp_import_prebuilt_simulator("${ESP_GSP_ROOT}")

file(GLOB GSP_APP_SOURCES CONFIGURE_DEPENDS "${GSP_APP_DIR}/*.c")
if(NOT GSP_APP_SOURCES)
    message(FATAL_ERROR "No application C sources in GSP_APP_DIR=${GSP_APP_DIR}")
endif()

add_executable(gsp_app_sim
    gsp_app_sim.c
    gsp_sim_gpio.c
    ${GSP_APP_SOURCES})
target_compile_features(gsp_app_sim PRIVATE c_std_11)
target_include_directories(gsp_app_sim PRIVATE
    "${GSP_APP_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${ESP_GSP_ROOT}/include")
target_link_libraries(gsp_app_sim PRIVATE
    esp_gsp::simulator_core
    esp_gsp::adapter_sdl
    m)
target_compile_definitions(gsp_app_sim PRIVATE
    GSP_APP_BUNDLE_PATH="/scene/preview.gspb")

if(EMSCRIPTEN)
    if(NOT EXISTS "${GSP_APP_BUNDLE}")
        message(FATAL_ERROR
            "GSP_APP_BUNDLE is missing: ${GSP_APP_BUNDLE}. "
            "Pack the scene before building the WASM host.")
    endif()
    set_target_properties(gsp_app_sim PROPERTIES SUFFIX ".html")
    set_property(TARGET gsp_app_sim APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../web/shell.html"
        "${GSP_APP_BUNDLE}")
    target_compile_options(gsp_app_sim PRIVATE "-sUSE_SDL=2")
    target_link_options(gsp_app_sim PRIVATE
        "-sUSE_SDL=2"
        "-sUSE_ZLIB=1"
        "-sUSE_LIBJPEG=1"
        "-sUSE_FREETYPE=1"
        "-sASYNCIFY"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_FUNCTIONS=_main,_gsp_app_sim_gpio_set,_gsp_app_sim_gpio_armed"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap"
        "--preload-file" "${GSP_APP_BUNDLE}@/scene/preview.gspb"
        "--shell-file" "${CMAKE_CURRENT_LIST_DIR}/../web/shell.html")
endif()
