if(POLICY CMP0084)
  # Disable trying to search for Qt3/4 if what we actually want is not found
  cmake_policy(SET CMP0084 NEW)
endif()

if (NOT QT_DIR AND MSVC)
  if(_M_ARM_64)
    set(QT_DIR "${CMAKE_SOURCE_DIR}/Externals/Qt/Qt6.3.0/ARM64/lib/cmake/Qt6")
  else()
    set(QT_DIR "${CMAKE_SOURCE_DIR}/Externals/Qt/Qt6.3.0/x64/lib/cmake/Qt6")
  endif()
endif()

set(CMAKE_AUTOMOC ON)

# For some reason the method in Qt6 documentation is not working (at least on ubuntu jammy)
# When Qt5 and Qt6 are given in same NAMES entry, only Qt5 is ever found.
find_package(QT NAMES Qt6 COMPONENTS Core Gui Widgets)
if(NOT QT_DIR)
  find_package(QT NAMES Qt5 COMPONENTS Core Gui Widgets)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
message(STATUS "Found Qt version ${QT_VERSION}")

add_library(scripting
  ScriptingEngine.cpp
  ScriptingEngine.h
  Python/coroutine.cpp
  Python/coroutine.h
  Python/PyScriptingBackend.cpp
  Python/PyScriptingBackend.h
  Python/Modules/controllermodule.cpp
  Python/Modules/controllermodule.h
  Python/Modules/doliomodule.cpp
  Python/Modules/doliomodule.h
  Python/Modules/dolphinmodule.cpp
  Python/Modules/dolphinmodule.h
  Python/Modules/eventmodule.cpp
  Python/Modules/eventmodule.h
  Python/Modules/filemodule.cpp
  Python/Modules/filemodule.h
  Python/Modules/guimodule.cpp
  Python/Modules/guimodule.h
  Python/Modules/memorymodule.cpp
  Python/Modules/memorymodule.h
  Python/Modules/registersmodule.cpp
  Python/Modules/registersmodule.h
  Python/Modules/savestatemodule.cpp
  Python/Modules/savestatemodule.h
  Python/Utils/as_py_func.h
  Python/Utils/convert.h
  Python/Utils/fmt.h
  Python/Utils/invoke.h
  Python/Utils/module.h
  Python/Utils/object_wrapper.cpp
  Python/Utils/object_wrapper.h
)


find_package(Python3 REQUIRED COMPONENTS Development)
include_directories(${Python3_INCLUDE_DIRS} ${Qt${QT_VERSION_MAJOR}Gui_PRIVATE_INCLUDE_DIRS})
# TODO imgui shouldn't be here
target_link_libraries(scripting PRIVATE ${Python3_LIBRARIES} imgui core Qt${QT_VERSION_MAJOR}::Widgets)
