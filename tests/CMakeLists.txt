# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(project-smoke smoke/project_smoke.cpp)
target_link_libraries(project-smoke PRIVATE Qt6::Core Qt6::Test)
add_test(NAME project-smoke COMMAND project-smoke)

add_executable(device-parsers-test unit/device_parsers_test.cpp)
target_include_directories(device-parsers-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(device-parsers-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME device-parsers COMMAND device-parsers-test)
