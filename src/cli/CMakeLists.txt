# SPDX-License-Identifier: GPL-3.0-or-later

add_library(rokudeck-cli STATIC
  command_line.cpp
  dbus_client.cpp
)
add_library(RokuDeck::Cli ALIAS rokudeck-cli)
target_include_directories(rokudeck-cli PUBLIC ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rokudeck-cli
  PUBLIC
    RokuDeck::DBusInterface
    Qt6::Core
    Qt6::DBus
)

add_executable(rokudeckctl main.cpp)
target_link_libraries(rokudeckctl PRIVATE RokuDeck::Cli)
target_compile_definitions(rokudeckctl PRIVATE ROKUDECK_VERSION="${PROJECT_VERSION}")
set_target_properties(rokudeckctl PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
install(TARGETS rokudeckctl RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
