# SPDX-License-Identifier: GPL-3.0-or-later

add_library(rokudeck-core STATIC
  active_app_parser.cpp
  audio_device_parser.cpp
  device_info_parser.cpp
  device_registry.cpp
  device_state.cpp
  media_player_parser.cpp
  poll_scheduler.cpp
  process_runner.h
  qprocess_runner.cpp
  rds_client.cpp
)

add_library(RokuDeck::Core ALIAS rokudeck-core)

target_include_directories(rokudeck-core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(rokudeck-core
  PUBLIC
    Qt6::Core
)
