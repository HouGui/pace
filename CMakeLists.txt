cmake_minimum_required(VERSION 3.20)
project(pace_host LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "Host tests require GCC or Clang. On Windows, use WSL2.")
endif()
option(PACE_SANITIZERS "Address and undefined-behavior sanitizers" OFF)
set(CJSON_SOURCE_DIR "" CACHE PATH "Existing cJSON checkout; otherwise fetch SDK-pinned commit")
if(NOT CJSON_SOURCE_DIR)
  include(FetchContent)
  FetchContent_Declare(cjson_source
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG b2890c8d76bbb64e710585ebc0a917196b9c67e7
    # Download source only; the small library below avoids upstream test/install targets.
    SOURCE_SUBDIR pace-download-only)
  FetchContent_MakeAvailable(cjson_source)
  set(CJSON_SOURCE_DIR "${cjson_source_SOURCE_DIR}")
endif()
if(PACE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
add_library(cjson STATIC "${CJSON_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${CJSON_SOURCE_DIR}")
add_library(pace_core firmware/components/pace_core/src/core.cpp firmware/components/pace_core/src/calendar_view.cpp firmware/components/pace_core/src/map.cpp)
target_include_directories(pace_core PUBLIC firmware/components/pace_core/include)
add_library(pace_protocol firmware/components/pace_protocol/json.cpp
  firmware/components/pace_protocol/feed.cpp firmware/components/pace_protocol/settings.cpp
  firmware/components/pace_protocol/settings_edit.cpp firmware/components/pace_protocol/traffic.cpp
  firmware/components/pace_protocol/traffic_settings.cpp firmware/components/pace_protocol/osrm.cpp
  firmware/components/pace_protocol/transit.cpp firmware/components/pace_protocol/route_geometry.cpp
  firmware/components/pace_protocol/weather.cpp)
target_include_directories(pace_protocol PUBLIC firmware/components/pace_protocol/include)
target_link_libraries(pace_protocol PUBLIC pace_core cjson)
foreach(target pace_core pace_protocol)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
endforeach()
enable_testing()
add_executable(pace_tests tests/host/test_main.cpp)
target_link_libraries(pace_tests PRIVATE pace_protocol)
target_compile_definitions(pace_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(pace_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME domain_and_protocol COMMAND pace_tests)
add_executable(pace_feed_check tools/feed_check.cpp)
target_link_libraries(pace_feed_check PRIVATE pace_protocol)
add_test(NAME feed_fixture COMMAND pace_feed_check
  "${CMAKE_CURRENT_SOURCE_DIR}/docs/interfaces/calendar-feed.example.json")
add_executable(pace_http_tests tests/host/test_http.cpp
  firmware/components/pace_services/http.cpp)
target_include_directories(pace_http_tests PRIVATE tests/host/http_stubs
  firmware/components/pace_services/include)
target_link_libraries(pace_http_tests PRIVATE pace_protocol)
target_compile_options(pace_http_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME http_transport COMMAND pace_http_tests)
add_executable(pace_portal_address_tests tests/host/test_portal_address.cpp)
target_include_directories(pace_portal_address_tests PRIVATE firmware/components/pace_services)
target_compile_options(pace_portal_address_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME portal_address COMMAND pace_portal_address_tests)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
foreach(profile signed unsigned)
  add_executable(pace_ota_${profile}_tests tests/host/test_ota.cpp
    firmware/components/pace_services/ota.cpp)
  target_include_directories(pace_ota_${profile}_tests PRIVATE tests/host/ota_stubs
    tests/host/http_stubs firmware/components/pace_services/include
    firmware/components/pace_board/include)
  target_link_libraries(pace_ota_${profile}_tests PRIVATE pace_protocol OpenSSL::Crypto)
  target_compile_options(pace_ota_${profile}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(profile STREQUAL "unsigned")
    target_compile_definitions(pace_ota_${profile}_tests PRIVATE PACE_TEST_UNSIGNED)
  endif()
  add_test(NAME ota_${profile} COMMAND pace_ota_${profile}_tests)
endforeach()
add_executable(pace_calendar_view_tests tests/host/test_calendar_view.cpp)
target_link_libraries(pace_calendar_view_tests PRIVATE pace_core)
target_compile_options(pace_calendar_view_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME calendar_view COMMAND pace_calendar_view_tests)

set(PACE_LVGL_SOURCE_DIR "" CACHE PATH "Optional existing pinned LVGL 9.4.0 source for display tests")
if(PACE_LVGL_SOURCE_DIR)
  set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/ui_stubs/lv_conf.h" CACHE PATH "" FORCE)
  set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${PACE_LVGL_SOURCE_DIR}" lvgl EXCLUDE_FROM_ALL)
  add_executable(pace_ui_tests tests/host/test_ui.cpp
    firmware/components/pace_ui/controls.cpp firmware/components/pace_ui/agenda.cpp
    firmware/components/pace_ui/month.cpp firmware/components/pace_ui/view.cpp
    firmware/components/pace_ui/traffic.cpp
    firmware/components/pace_ui/route_map.cpp firmware/components/pace_ui/map_tiles.cpp
    firmware/components/pace_services/map_png.cpp
    firmware/components/pace_ui/brightness.cpp
    firmware/components/pace_ui/fonts/pace_font_16.c
    firmware/components/pace_ui/fonts/pace_font_24.c
    firmware/components/pace_ui/fonts/pace_font_32.c)
  target_include_directories(pace_ui_tests PRIVATE tests/host/ui_stubs tests/host/ota_stubs
    tests/host/http_stubs firmware/components/pace_ui firmware/components/pace_ui/include
    firmware/components/pace_services/include)
  target_compile_definitions(pace_ui_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(pace_ui_tests PRIVATE pace_protocol lvgl::lvgl)
  target_compile_options(pace_ui_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME display_controls COMMAND pace_ui_tests)
endif()
add_executable(pace_commands_tests tests/host/test_commands.cpp
  firmware/components/pace_services/commands.cpp firmware/components/pace_services/upload.cpp)
target_include_directories(pace_commands_tests PRIVATE tests/host/ui_stubs tests/host/http_stubs
  firmware/components/pace_services firmware/components/pace_services/include)
target_link_libraries(pace_commands_tests PRIVATE pace_protocol)
target_compile_options(pace_commands_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME commands_and_upload COMMAND pace_commands_tests)

add_executable(pace_traffic_tests tests/host/test_traffic.cpp
  firmware/components/pace_services/traffic.cpp)
target_include_directories(pace_traffic_tests PRIVATE tests/host/ui_stubs tests/host/http_stubs
  tests/host/ota_stubs
  firmware/components/pace_services firmware/components/pace_services/include)
target_link_libraries(pace_traffic_tests PRIVATE pace_protocol)
target_compile_definitions(pace_traffic_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(pace_traffic_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME traffic COMMAND pace_traffic_tests)
add_executable(pace_open_routes_tests tests/host/test_open_routes.cpp)
target_link_libraries(pace_open_routes_tests PRIVATE pace_protocol)
target_compile_definitions(pace_open_routes_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(pace_open_routes_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME open_routes COMMAND pace_open_routes_tests)
add_executable(pace_brightness_tests tests/host/test_brightness.cpp
  firmware/components/pace_services/brightness.cpp)
target_include_directories(pace_brightness_tests PRIVATE tests/host/ui_stubs tests/host/http_stubs
  firmware/components/pace_services firmware/components/pace_services/include)
target_link_libraries(pace_brightness_tests PRIVATE pace_protocol)
target_compile_options(pace_brightness_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME brightness COMMAND pace_brightness_tests)

add_executable(pace_maps_tests tests/host/test_maps.cpp
  firmware/components/pace_services/maps.cpp firmware/components/pace_services/map_png.cpp)
target_include_directories(pace_maps_tests PRIVATE tests/host/map_stubs tests/host/ui_stubs
  tests/host/http_stubs tests/host/ota_stubs firmware/components/pace_services
  firmware/components/pace_services/include)
target_link_libraries(pace_maps_tests PRIVATE pace_protocol)
target_compile_definitions(pace_maps_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(pace_maps_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME maps COMMAND pace_maps_tests)

add_executable(pace_portal_tests tests/host/test_portal.cpp
  firmware/components/pace_services/portal.cpp)
target_include_directories(pace_portal_tests PRIVATE tests/host/portal_stubs tests/host/ui_stubs
  tests/host/http_stubs tests/host/ota_stubs firmware/components/pace_services
  firmware/components/pace_services/include)
target_link_libraries(pace_portal_tests PRIVATE pace_protocol)
target_compile_definitions(pace_portal_tests PRIVATE PACE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(pace_portal_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME setup_portal COMMAND pace_portal_tests)
