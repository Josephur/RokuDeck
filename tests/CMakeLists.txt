add_executable(project-smoke smoke/project_smoke.cpp)
target_link_libraries(project-smoke PRIVATE Qt6::Core Qt6::Test)
add_test(NAME project-smoke COMMAND project-smoke)
