# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(core)
add_subdirectory(daemon)
add_subdirectory(cli)
if(ROKUDECK_BUILD_PLASMOID)
  add_subdirectory(plasmoid)
endif()
