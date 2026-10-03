cmake_minimum_required(VERSION 3.20)
project(coop_backend LANGUAGES C)

if(NOT ESP_GSP_COMPONENT_DIR AND DEFINED ENV{ESP_GSP_COMPONENT_DIR})
    set(ESP_GSP_COMPONENT_DIR "$ENV{ESP_GSP_COMPONENT_DIR}")
endif()
if(NOT ESP_GSP_COMPONENT_DIR)
    foreach(candidate IN ITEMS
            "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp-gsp")
        get_filename_component(candidate "${candidate}" ABSOLUTE)
        if(EXISTS "${candidate}/tools/sim_bridge/CMakeLists.txt")
            set(ESP_GSP_COMPONENT_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge/CMakeLists.txt")
    message(FATAL_ERROR
        "Install espressif/esp-gsp first, or set ESP_GSP_COMPONENT_DIR")
endif()

add_subdirectory("${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge" sim_bridge)
gsp_add_backend(coop_backend
    SOURCES ../main/coop_state.c ../main/coop_body.c ../main/coop_fx.c ../main/coop_touch.c ../main/coop_carry.c ../main/coop_bubbles.c ../main/coop_hud.c ../main/coop_render.c ../main/coop_animation.c ../main/coop_ui.c ../main/coop_subtitle.c platform_pc.c link_pc.c ../main/coop_script.c ../managed_components/espressif__cjson/cJSON/cJSON.c
    INCLUDE_DIRECTORIES ../main ../managed_components/espressif__cjson/cJSON
    SCENES ../ui/main.json)
target_compile_definitions(coop_backend PRIVATE COOP_ATLAS_PATH="${CMAKE_CURRENT_LIST_DIR}/../main/coo-atlas.bin")
# The simulator reports the same version as the firmware's app descriptor.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults" coop_version_line REGEX "^CONFIG_APP_PROJECT_VER=")
string(REGEX REPLACE "^CONFIG_APP_PROJECT_VER=\"([^\"]+)\"$" "\\1" coop_version "${coop_version_line}")
target_compile_definitions(coop_backend PRIVATE COOP_APP_VERSION="${coop_version}")
