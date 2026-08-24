# SPDX-License-Identifier: GPL-3.0-or-later

find_package(KF6 6.0 REQUIRED COMPONENTS I18n ItemModels)
find_package(Plasma 6.0 REQUIRED)
find_package(Qt6 6.7 REQUIRED COMPONENTS QuickTest)

add_executable(project-smoke smoke/project_smoke.cpp)
target_link_libraries(project-smoke PRIVATE Qt6::Core Qt6::Test)
add_test(NAME project-smoke COMMAND project-smoke)

find_program(BASH_EXECUTABLE bash REQUIRED)
add_test(NAME public-privacy
  COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/privacy/public_repository_scan.sh
)
set_tests_properties(public-privacy PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME public-contract
  COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/project/public_contract_test.sh
)
set_tests_properties(public-contract PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(device-parsers-test unit/device_parsers_test.cpp)
target_include_directories(device-parsers-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(device-parsers-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME device-parsers COMMAND device-parsers-test)

add_executable(rule-model-test unit/rule_model_test.cpp)
# Match the include/define block shape of device-parsers-test above it.
target_include_directories(rule-model-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(rule-model-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME rule-model COMMAND rule-model-test)

add_executable(rule-scheduler-test unit/rule_scheduler_test.cpp)
target_include_directories(rule-scheduler-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(rule-scheduler-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME rule-scheduler COMMAND rule-scheduler-test)

add_executable(rule-summary-test unit/rule_summary_test.cpp)
target_include_directories(rule-summary-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(rule-summary-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME rule-summary COMMAND rule-summary-test)

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

add_executable(config-store-test unit/config_store_test.cpp)
target_include_directories(config-store-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(config-store-test PRIVATE RokuDeck::Daemon Qt6::Core Qt6::Test)
add_test(NAME config-store COMMAND config-store-test)

add_executable(poll-scheduler-test unit/poll_scheduler_test.cpp)
target_include_directories(poll-scheduler-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(poll-scheduler-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME poll-scheduler COMMAND poll-scheduler-test)

add_executable(devices-model-test
  unit/devices_model_test.cpp
  ${PROJECT_SOURCE_DIR}/src/plasmoid/devicesmodel.cpp
)
target_include_directories(devices-model-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(devices-model-test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME devices-model COMMAND devices-model-test)

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

# A private session bus still reads D-Bus service files from XDG_DATA_DIRS, so
# without this a developer who has RokuDeck installed would have their real
# daemon activated into the test bus and silently take over the service name.
set(ROKUDECK_ISOLATED_XDG "${CMAKE_CURRENT_BINARY_DIR}/xdg-isolated")
file(MAKE_DIRECTORY "${ROKUDECK_ISOLATED_XDG}")
set(ROKUDECK_ISOLATED_ENVIRONMENT
  "XDG_DATA_DIRS=${ROKUDECK_ISOLATED_XDG}"
  "XDG_DATA_HOME=${ROKUDECK_ISOLATED_XDG}"
)
set_tests_properties(daemon-dbus PROPERTIES ENVIRONMENT "${ROKUDECK_ISOLATED_ENVIRONMENT}")

add_executable(plasmoid-bridge-test
  unit/plasmoid_bridge_test.cpp
  ${PROJECT_SOURCE_DIR}/src/plasmoid/rokudeckapplet.cpp
  ${PROJECT_SOURCE_DIR}/src/plasmoid/daemonproxy.cpp
  ${PROJECT_SOURCE_DIR}/src/plasmoid/devicesmodel.cpp
)
target_include_directories(plasmoid-bridge-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plasmoid
)
target_compile_definitions(plasmoid-bridge-test
  PRIVATE TRANSLATION_DOMAIN="plasma_applet_io.github.josephur.rokudeck"
)
target_link_libraries(plasmoid-bridge-test
  PRIVATE
    RokuDeck::DBusInterface
    KF6::ConfigCore
    KF6::I18n
    Plasma::Plasma
    Qt6::Core
    Qt6::DBus
    Qt6::Qml
    Qt6::Test
)
add_test(NAME plasmoid-bridge
  COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:plasmoid-bridge-test>
)

add_executable(plasmoid-qml-test qml/qmltest.cpp)
target_link_libraries(plasmoid-qml-test
  PRIVATE
    KF6::I18n
    KF6::I18nQml
    Plasma::Plasma
    Qt6::Qml
    Qt6::QuickTest
)
add_test(NAME plasmoid-qml
  COMMAND plasmoid-qml-test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml
)
set_tests_properties(plasmoid-qml PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

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
set_tests_properties(rokudeckctl PROPERTIES ENVIRONMENT "${ROKUDECK_ISOLATED_ENVIRONMENT}")
set_tests_properties(plasmoid-bridge PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${ROKUDECK_ISOLATED_ENVIRONMENT}"
)
add_dependencies(rokudeckctl-test rokudeckctl)
