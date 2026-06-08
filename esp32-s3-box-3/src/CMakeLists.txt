set(COMMON_SRC "main.cpp" "deepmem_ws.cpp" "hardware_audio.cpp" "screen.cpp" "cellular.cpp")

if(IDF_TARGET STREQUAL linux)
	idf_component_register(
		SRCS ${COMMON_SRC}
		REQUIRES esp-tls tcp_transport)
else()
	idf_component_register(
		SRCS ${COMMON_SRC} "wifi.cpp"
		REQUIRES driver esp_wifi nvs_flash esp_psram esp-tls tcp_transport mbedtls lvgl esp_lcd espressif__esp_lvgl_port esp-ml307 uart-uhci)
endif()
