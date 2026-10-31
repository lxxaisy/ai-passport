cmake_minimum_required(VERSION 3.16)
project(agent_monitor_ui C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../managed_components/lvgl__lvgl" lvgl)

add_executable(am_ui_render
    render.c
    ../../main/am_view.c
    ../../main/am_model.c
    ../../main/am_protocol.c
    ../../assets/fonts/am_font_16.c
    ../../components/bsp/src/bsp_display_rounding.c
    ../vendor/cjson/cJSON.c
)
target_include_directories(am_ui_render PRIVATE ../../main ../vendor/cjson ../../components/bsp/src)
target_link_libraries(am_ui_render PRIVATE lvgl::lvgl m)
target_compile_options(am_ui_render PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(../vendor/cjson/cJSON.c PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
