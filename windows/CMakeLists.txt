cmake_minimum_required(VERSION 3.16)
project(stacks-ide-windows CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(WARNING "windows/ target requires a Windows toolchain. Skipping.")
  return()
endif()

add_executable(stacks-ide WIN32 main.cpp)
target_compile_definitions(stacks-ide PRIVATE UNICODE _UNICODE)

# WebView2 — expects SDK from NuGet or a local SDK folder.
# Set WEBVIEW2_SDK to the extracted SDK path (containing build\native\include).
if(DEFINED ENV{WEBVIEW2_SDK})
  target_include_directories(stacks-ide PRIVATE "$ENV{WEBVIEW2_SDK}/build/native/include")
  target_link_directories(stacks-ide PRIVATE "$ENV{WEBVIEW2_SDK}/build/native/x64")
endif()

target_link_libraries(stacks-ide PRIVATE
  WebView2LoaderStatic
  dwmapi shlwapi user32 ole32
)
