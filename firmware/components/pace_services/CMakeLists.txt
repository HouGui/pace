idf_component_register(SRCS "storage.cpp" "network.cpp" "http.cpp" "startup.cpp" "demo.cpp" "portal.cpp" "ota.cpp" "upload.cpp" "commands.cpp" "traffic.cpp" "brightness.cpp" "maps.cpp" "map_png.cpp"
  INCLUDE_DIRS "include"
  REQUIRES pace_core pace_protocol freertos esp_common
  PRIV_REQUIRES pace_board esp_wifi esp_netif esp_event esp_http_client esp_http_server
                app_update nvs_flash nvs_sec_provider esp_timer esp_system mbedtls esp_app_format
  EMBED_TXTFILES "setup.html")

# HTTP handlers share an 8 KiB task stack with SDK calls. Catch oversized local snapshots.
set_source_files_properties(portal.cpp PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=3072")
set_source_files_properties(http.cpp PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=1024")
set_source_files_properties(traffic.cpp PROPERTIES COMPILE_OPTIONS "-Werror=frame-larger-than=4096")
