# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)
project(RokuDeck VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core DBus Network Test)
find_package(KF6 6.0 REQUIRED COMPONENTS Config CoreAddons)

option(ROKUDECK_BUILD_TESTS "Build RokuDeck tests" ON)
option(ROKUDECK_ENABLE_DEVELOPER_MODE "Enable strict developer warnings" ON)
# Daemon-only hosts (headless VMs, servers) have no Plasma and still want
# rokudeckd + rokudeckctl; the tests exercise the plasmoid bridge and need it.
option(ROKUDECK_BUILD_PLASMOID "Build the Plasma widget" ON)

include(cmake/ProjectOptions.cmake)
add_subdirectory(src)
if(ROKUDECK_BUILD_TESTS)
  if(NOT ROKUDECK_BUILD_PLASMOID)
    message(FATAL_ERROR "ROKUDECK_BUILD_TESTS requires ROKUDECK_BUILD_PLASMOID (the bridge tests link the widget)")
  endif()
  enable_testing()
  add_subdirectory(tests)
endif()
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
