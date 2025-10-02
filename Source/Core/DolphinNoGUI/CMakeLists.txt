add_executable(dolphin-nogui
  MainNoGUI.cpp
)

target_link_libraries(dolphin-nogui
PRIVATE
  platformcommon
  core
  uicommon
  cpp-optparse
)

if(WIN32)
  # needed for adjusting window decorations with DwmSetWindowAttribute
  target_link_libraries(dolphin-nogui PRIVATE dwmapi.lib)
endif()

if(MSVC)
  # Add precompiled header
  target_link_libraries(dolphin-nogui PRIVATE use_pch)
endif()

if(USE_DISCORD_PRESENCE)
  target_compile_definitions(dolphin-nogui PRIVATE -DUSE_DISCORD_PRESENCE)
endif()

set(CPACK_PACKAGE_EXECUTABLES ${CPACK_PACKAGE_EXECUTABLES} dolphin-nogui)
install(TARGETS dolphin-nogui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

