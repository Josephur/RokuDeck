# SPDX-License-Identifier: GPL-3.0-or-later

include_guard(GLOBAL)

if(ROKUDECK_ENABLE_DEVELOPER_MODE)
  add_compile_options(
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Werror>"
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/WX>"
  )
endif()
