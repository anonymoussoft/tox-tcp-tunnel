cmake_minimum_required(VERSION 3.14)
project(toxtunnel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(TOXTUNNEL_BUILD_TESTS "Build tests" ON)
option(TOXTUNNEL_ENABLE_ASAN "Enable AddressSanitizer" OFF)

# Sanitizer flags (applied globally since they affect linking too)
if(NOT MSVC AND TOXTUNNEL_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(TOXCORE REQUIRED toxcore)

include(FetchContent)

# Asio (standalone, header-only)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# CLI11
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.3.2
)
FetchContent_MakeAvailable(cli11)

# yaml-cpp
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.7.0
)
FetchContent_MakeAvailable(yaml-cpp)

# Library target
add_library(toxtunnel_lib STATIC)
target_sources(toxtunnel_lib PRIVATE
    src/placeholder.cpp
    src/core/io_context.cpp
    src/core/tcp_connection.cpp
    src/core/tcp_listener.cpp
    src/tunnel/protocol.cpp
    src/tunnel/tunnel.cpp
    src/tunnel/tunnel_manager.cpp
    src/tox/types.cpp
    src/tox/tox_save.cpp
    src/tox/tox_thread.cpp
    src/tox/tox_connection.cpp
    src/tox/tox_adapter.cpp
    src/app/rules_engine.cpp
    src/app/tunnel_client.cpp
    src/app/tunnel_server.cpp
    src/util/config.cpp
    src/util/error.cpp
    src/util/logger.cpp
)
target_include_directories(toxtunnel_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include
    ${TOXCORE_INCLUDE_DIRS}
)
target_link_directories(toxtunnel_lib PUBLIC
    ${TOXCORE_LIBRARY_DIRS}
)
target_link_libraries(toxtunnel_lib PUBLIC
    ${TOXCORE_LIBRARIES}
    spdlog::spdlog
    yaml-cpp
)
target_compile_definitions(toxtunnel_lib PUBLIC
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)
# Apply strict warnings only to our own library target
if(MSVC)
    target_compile_options(toxtunnel_lib PRIVATE /W4 /WX)
else()
    target_compile_options(toxtunnel_lib PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Executable target
add_executable(toxtunnel cli/main.cpp)
target_link_libraries(toxtunnel PRIVATE toxtunnel_lib CLI11::CLI11)
if(MSVC)
    target_compile_options(toxtunnel PRIVATE /W4 /WX)
else()
    target_compile_options(toxtunnel PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Tests
if(TOXTUNNEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS toxtunnel DESTINATION bin)
