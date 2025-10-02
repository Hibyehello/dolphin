add_library(platformcommon
  Platform.cpp
  Platform.h
  PlatformHeadless.cpp
)

if(X11_FOUND)
  target_sources(platformcommon PRIVATE PlatformX11.cpp)
  target_link_libraries(dolphin-nogui PRIVATE PkgConfig::XRANDR PkgConfig::X11)
endif()

if(WIN32)
  target_sources(platformcommon PRIVATE PlatformWin32.cpp)
endif()

if(APPLE)
  target_sources(platformcommon PRIVATE PlatformMacos.mm)
  target_compile_options(platformcommon PRIVATE -fobjc-arc)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  target_sources(platformcommon PRIVATE PlatformFBDev.cpp)
endif()

target_link_libraries(platformcommon
PRIVATE
  core
  uicommon
  cpp-optparse
)

if(APPLE)
  target_link_libraries(platformcommon
    PRIVATE
      ${APPKIT_LIBRARY}
      ${COREFOUNDATION_LIBRARY}
      ${IOK_LIBRARY}
  )
endif()

