add_executable(dolphin-Hib
  MainHib.cpp
)

set_target_properties(dolphin-Hib PROPERTIES OUTPUT_NAME dolphin-emu-hib)

target_link_libraries(dolphin-Hib
PRIVATE
  platformcommon
  core
  imgui
  uicommon
  cpp-optparse
)

if(WIN32)
  # needed for adjusting window decorations with DwmSetWindowAttribute
  target_link_libraries(dolphin-Hib PRIVATE dwmapi.lib)
endif()

if(MSVC)
  # Add precompiled header
  target_link_libraries(dolphin-Hib PRIVATE use_pch)
endif()

if(USE_DISCORD_PRESENCE)
  target_compile_definitions(dolphin-Hib PRIVATE -DUSE_DISCORD_PRESENCE)
endif()

set(CPACK_PACKAGE_EXECUTABLES ${CPACK_PACKAGE_EXECUTABLES} dolphin-Hib)
install(TARGETS dolphin-Hib RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})