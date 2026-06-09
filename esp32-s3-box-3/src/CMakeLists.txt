set(COMMON_SRC "main.cpp" "deepmem_ws.cpp" "hardware_audio.cpp" "screen.cpp" "cellular.cpp" "sdcard.cpp" "audio_storage.cpp" "song_player.cpp" "vad.cpp" "vad_serial_test.cpp")

if(IDF_TARGET STREQUAL linux)
	idf_component_register(
		SRCS ${COMMON_SRC}
		REQUIRES esp-tls tcp_transport espressif__esp-sr fatfs sdmmc esp_driver_sdmmc esp32-idf-sqlite3)
else()
	idf_component_register(
		SRCS ${COMMON_SRC} "wifi.cpp"
		REQUIRES driver esp_wifi nvs_flash esp_psram esp-tls tcp_transport mbedtls lvgl esp_lcd espressif__esp_lvgl_port esp-ml307 uart-uhci espressif__esp-sr fatfs sdmmc esp_driver_sdmmc esp32-idf-sqlite3)
endif()
