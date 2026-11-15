idf_component_register(SRCS "json.cpp" "feed.cpp" "settings.cpp" "settings_edit.cpp" "traffic.cpp"
  "traffic_settings.cpp" "osrm.cpp" "transit.cpp" "route_geometry.cpp" "weather.cpp"
  INCLUDE_DIRS "include" REQUIRES pace_core json)
