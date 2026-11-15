idf_component_register(SRCS "ui.cpp" "view.cpp" "agenda.cpp" "month.cpp" "controls.cpp" "traffic.cpp" "route_map.cpp" "map_tiles.cpp" "brightness.cpp"
  "fonts/pace_font_16.c" "fonts/pace_font_24.c" "fonts/pace_font_32.c" INCLUDE_DIRS "include"
  REQUIRES pace_services
  PRIV_REQUIRES pace_core pace_board lvgl esp_timer esp_app_format)
