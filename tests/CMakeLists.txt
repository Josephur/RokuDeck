# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(project-smoke smoke/project_smoke.cpp)
target_link_libraries(project-smoke PRIVATE Qt6::Core Qt6::Test)
add_test(NAME project-smoke COMMAND project-smoke)

add_executable(device-parsers-test unit/device_parsers_test.cpp)
target_include_directories(device-parsers-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(device-parsers-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME device-parsers COMMAND device-parsers-test)

add_executable(rds-process-helper support/process_helper.cpp)
target_link_libraries(rds-process-helper PRIVATE Qt6::Core)

add_executable(rds-client-test unit/rds_client_test.cpp)
target_include_directories(rds-client-test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rds-client-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_dependencies(rds-client-test rds-process-helper)
add_test(NAME rds-client COMMAND rds-client-test)

add_executable(device-registry-test unit/device_registry_test.cpp)
target_include_directories(device-registry-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(device-registry-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME device-registry COMMAND device-registry-test)

add_executable(poll-scheduler-test unit/poll_scheduler_test.cpp)
target_include_directories(poll-scheduler-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(poll-scheduler-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME poll-scheduler COMMAND poll-scheduler-test)

add_executable(daemon-dbus-test integration/daemon_dbus_test.cpp)
target_include_directories(daemon-dbus-test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(daemon-dbus-test
  PRIVATE
    RokuDeck::Daemon
    RokuDeck::DBusInterface
    KF6::ConfigCore
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
add_test(NAME daemon-dbus COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:daemon-dbus-test>)

add_executable(daemon-client-helper support/daemon_client_helper.cpp)
target_link_libraries(daemon-client-helper PRIVATE Qt6::Core Qt6::DBus)
add_dependencies(daemon-dbus-test daemon-client-helper rokudeckd)

add_executable(rokudeckctl-test integration/rokudeckctl_test.cpp)
target_include_directories(rokudeckctl-test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rokudeckctl-test
  PRIVATE ROKUDECKCTL_PATH="${CMAKE_BINARY_DIR}/src/cli/rokudeckctl"
)
target_link_libraries(rokudeckctl-test
  PRIVATE
    RokuDeck::Cli
    RokuDeck::Daemon
    KF6::ConfigCore
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)
add_test(NAME rokudeckctl
  COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:rokudeckctl-test>
)
add_dependencies(rokudeckctl-test rokudeckctl)
