# SPDX-License-Identifier: GPL-3.0-or-later

set(rokudeck_dbus_xml ${PROJECT_SOURCE_DIR}/data/dbus/io.github.josephur.RokuDeck1.xml)

set(rokudeck_daemon_sources
  client_registry.cpp
  config_store.cpp
  daemon_service.cpp
)
qt_add_dbus_adaptor(rokudeck_daemon_sources
  ${rokudeck_dbus_xml}
  daemon_service.h
  RokuDeck::DaemonService
  rokudeck1_adaptor
)
add_library(rokudeck-daemon STATIC ${rokudeck_daemon_sources})
add_library(RokuDeck::Daemon ALIAS rokudeck-daemon)
set_source_files_properties(daemon_service.cpp PROPERTIES
  OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rokudeck1_adaptor.h
)

target_include_directories(rokudeck-daemon
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(rokudeck-daemon
  PUBLIC
    RokuDeck::Core
    KF6::ConfigCore
    Qt6::Core
    Qt6::DBus
)

set(rokudeck_dbus_interface_sources)
qt_add_dbus_interface(rokudeck_dbus_interface_sources ${rokudeck_dbus_xml} rokudeck1_interface)
add_library(rokudeck-dbus-interface STATIC ${rokudeck_dbus_interface_sources})
add_library(RokuDeck::DBusInterface ALIAS rokudeck-dbus-interface)
target_include_directories(rokudeck-dbus-interface PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(rokudeck-dbus-interface PUBLIC Qt6::Core Qt6::DBus)

add_executable(rokudeckd main.cpp)
target_link_libraries(rokudeckd PRIVATE RokuDeck::Daemon)
install(TARGETS rokudeckd RUNTIME DESTINATION ${KDE_INSTALL_LIBEXECDIR})

configure_file(
  ${PROJECT_SOURCE_DIR}/data/dbus/io.github.josephur.RokuDeck1.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/io.github.josephur.RokuDeck1.service
  @ONLY
)
configure_file(
  ${PROJECT_SOURCE_DIR}/data/systemd/rokudeckd.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/rokudeckd.service
  @ONLY
)
install(FILES ${rokudeck_dbus_xml} DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.josephur.RokuDeck1.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rokudeckd.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
