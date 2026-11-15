idf_component_register(SRCS "board.c" INCLUDE_DIRS "include"
  REQUIRES esp_lcd esp_common
  PRIV_REQUIRES esp_driver_i2c esp_driver_gpio freertos)
