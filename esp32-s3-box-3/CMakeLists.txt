cmake_minimum_required(VERSION 3.19)

if(NOT IDF_TARGET STREQUAL linux)
  if(NOT DEFINED ENV{WIFI_SSID} OR NOT DEFINED ENV{WIFI_PASSWORD})
    message(FATAL_ERROR "Env variables WIFI_SSID and WIFI_PASSWORD must be set")
  endif()

  add_compile_definitions(WIFI_SSID="$ENV{WIFI_SSID}")
  add_compile_definitions(WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")
endif()

if(DEFINED ENV{LOG_DATACHANNEL_MESSAGES})
  add_compile_definitions(LOG_DATACHANNEL_MESSAGES="1")
endif()

if(DEFINED ENV{DEEPMEM_WS_HOST})
  add_compile_definitions(DEEPMEM_WS_HOST="$ENV{DEEPMEM_WS_HOST}")
endif()

if(DEFINED ENV{DEEPMEM_WS_PORT})
  add_compile_definitions(DEEPMEM_WS_PORT=$ENV{DEEPMEM_WS_PORT})
endif()

if(DEFINED ENV{DEEPMEM_WS_PATH})
  add_compile_definitions(DEEPMEM_WS_PATH="$ENV{DEEPMEM_WS_PATH}")
endif()

set(COMPONENTS src)
set(EXTRA_COMPONENT_DIRS "src" "components/srtp" "components/peer" "components/esp-libopus")

if(IDF_TARGET STREQUAL linux)
  add_compile_definitions(LINUX_BUILD=1)
  list(APPEND EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs
    "components/esp-protocols/common_components/linux_compat/esp_timer"
    "components/esp-protocols/common_components/linux_compat/freertos"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
