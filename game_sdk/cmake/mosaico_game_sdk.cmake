# Shared project integration for the in-tree Mosaico Game SDK.
set(MOSAICO_GAME_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

function(mosaico_game_sdk_configure_gsp_compiler)
    if(DEFINED GSPC_EXECUTABLE OR DEFINED ENV{GSPC_EXECUTABLE})
        return()
    endif()
    get_filename_component(_workspace_gspc
        "${MOSAICO_GAME_SDK_ROOT}/../../../esp-gsp/ci/gspc-dev" ABSOLUTE)
    if(EXISTS "${_workspace_gspc}")
        set(GSPC_EXECUTABLE "${_workspace_gspc}" CACHE FILEPATH
            "Standalone ESP-GSP scene compiler")
    endif()
endfunction()

function(mosaico_game_sdk_add_components)
    set(options RAYLIB AUDIO TILEMAP)
    cmake_parse_arguments(GAME "${options}" "" "" ${ARGN})

    set(_components mosaico_game mosaico_game_input mosaico_game_debug)
    if(GAME_RAYLIB OR GAME_AUDIO OR GAME_TILEMAP)
        list(APPEND _components mosaico_game_assets)
    endif()
    if(GAME_RAYLIB OR GAME_TILEMAP)
        list(APPEND _components mosaico_game_2d)
    endif()
    if(GAME_RAYLIB)
        list(APPEND _components mosaico_raylib_fast mosaico_raylib_port)
    endif()
    if(GAME_AUDIO)
        list(APPEND _components mosaico_game_audio)
    endif()
    if(GAME_TILEMAP)
        list(APPEND _components mosaico_game_tilemap)
    endif()

    foreach(_component IN LISTS _components)
        list(APPEND EXTRA_COMPONENT_DIRS
            "${MOSAICO_GAME_SDK_ROOT}/components/${_component}")
    endforeach()
    list(REMOVE_DUPLICATES EXTRA_COMPONENT_DIRS)
    set(EXTRA_COMPONENT_DIRS "${EXTRA_COMPONENT_DIRS}" PARENT_SCOPE)
endfunction()
