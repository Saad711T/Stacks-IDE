cmake_minimum_required(VERSION 3.16)
project(stacks-ide)

# Always build the backend daemon
add_subdirectory(core)

# Native shell — builds only on its matching platform
if(WIN32)
  add_subdirectory(windows)
elseif(UNIX AND NOT APPLE)
  add_subdirectory(linux)
endif()
