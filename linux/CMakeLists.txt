cmake_minimum_required(VERSION 3.16)
project(stacks-ide-linux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT UNIX OR APPLE)
  message(WARNING "linux/ target requires Linux. Skipping.")
  return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkitgtk-6.0)

add_executable(stacks-ide main.cpp)
target_include_directories(stacks-ide PRIVATE ${GTK4_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
target_link_libraries(stacks-ide PRIVATE ${GTK4_LIBRARIES} PkgConfig::WEBKIT)
target_compile_options(stacks-ide PRIVATE ${GTK4_CFLAGS_OTHER})
