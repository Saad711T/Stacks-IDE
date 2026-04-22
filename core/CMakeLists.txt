cmake_minimum_required(VERSION 3.16)
project(stacksd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W3 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include(FetchContent)

# --- cpp-httplib (single header HTTP server) ---
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(cpp_httplib)

# --- nlohmann/json (single header) ---
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------- sources ----------------
set(SRC
  src/server.cpp
  src/runner.cpp
)

if(WIN32)
  list(APPEND SRC src/pty_win.cpp)
else()
  list(APPEND SRC src/pty_posix.cpp)
endif()

add_executable(stacksd ${SRC})
target_include_directories(stacksd PRIVATE include)
target_link_libraries(stacksd PRIVATE httplib::httplib nlohmann_json::nlohmann_json)

if(WIN32)
  target_link_libraries(stacksd PRIVATE ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(stacksd PRIVATE Threads::Threads util)
endif()

install(TARGETS stacksd RUNTIME DESTINATION bin)
