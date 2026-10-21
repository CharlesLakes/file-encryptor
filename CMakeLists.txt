# FileEncryptor build.
#
#   cmake -B build                 configure (Release with debug info by default)
#   cmake --build build            build build/file-encryptor
#   ctest --test-dir build         run the tests (no display needed)
#
# The platform is chosen from the target system: Windows uses the Win32 GUI
# backend and the Windows file layer, everything else uses Xlib and POSIX.
#
# The code is C++20, built without exceptions or RTTI. On Linux only the
# runtime libraries are needed (libX11.so.6 and libsodium.so.23); their
# declarations live in src/deps/, so no -dev packages are required. On Windows the Win32 API comes with MinGW-w64 and libsodium is
# linked through its import library (libsodium.dll.a).

cmake_minimum_required(VERSION 3.16)
project(FileEncryptor LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json lets clangd find the include path and flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- Platform ----------------

if(WIN32)
  set(OS_SRC  src/core/fs_win32.cpp src/core/net_win32.cpp src/core/http_winhttp.cpp)
  set(GUI_SRC src/ui/platform_win32.cpp)
  set(TTY_SRC src/cli/tty_win32.cpp)
  find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED)
  set(SYSTEM_LIBS ws2_32 winhttp)
  set(GUI_LIBS gdi32 user32)
  # NOMINMAX keeps <windows.h> from defining min() and max() macros.
  set(PLATFORM_DEFS __USE_MINGW_ANSI_STDIO=1 NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  set(OS_SRC  src/core/fs_posix.cpp src/core/net_posix.cpp src/core/http_curl.cpp)
  set(GUI_SRC src/ui/platform_x11.cpp)
  set(TTY_SRC src/cli/tty_posix.cpp)
  # Link the runtime libraries by their exact file names.
  find_library(SODIUM_LIB NAMES libsodium.so.23 sodium REQUIRED)
  find_library(X11_LIB NAMES libX11.so.6 X11 REQUIRED)
  # libcurl is loaded with dlopen() (see src/deps/curl.hpp).
  set(SYSTEM_LIBS ${CMAKE_DL_LIBS})
  set(GUI_LIBS ${X11_LIB})
  set(PLATFORM_DEFS "")
endif()

# ---------------- Core (everything except the GUI) ----------------

add_library(crls_core STATIC
  src/core/append.cpp
  src/core/archive.cpp
  src/core/browser.cpp
  src/core/httpmsg.cpp
  src/core/remote.cpp
  src/core/source.cpp
  src/core/status.cpp
  src/core/util.cpp
  ${OS_SRC}
)
target_include_directories(crls_core PUBLIC src)
target_compile_definitions(crls_core PUBLIC _DEFAULT_SOURCE ${PLATFORM_DEFS})
# Errors are reported with set_status() and return values, never with
# exceptions; nothing needs run-time type information either.
target_compile_options(crls_core PUBLIC -Wall -Wextra -fno-exceptions -fno-rtti)
# Link the C++ runtime statically on MinGW, so the executables only need
# libsodium-26.dll next to them.
if(MINGW)
  target_link_options(crls_core PUBLIC -static-libgcc -static-libstdc++)
endif()
target_link_libraries(crls_core PUBLIC ${SODIUM_LIB} ${SYSTEM_LIBS})

# ---------------- Server ----------------

add_library(crls_server STATIC src/server/server.cpp)
target_link_libraries(crls_server PUBLIC crls_core)

# ---------------- Command-line client ----------------

add_library(crls_cli STATIC src/cli/cli.cpp ${TTY_SRC})
target_link_libraries(crls_cli PUBLIC crls_core)

# ---------------- Program ----------------

set(UI_SRC
  src/ui/actions.cpp
  src/ui/draw.cpp
  src/ui/input.cpp
  src/ui/layout.cpp
  ${GUI_SRC}
)

add_executable(file-encryptor WIN32 src/main.cpp ${UI_SRC})
target_link_libraries(file-encryptor PRIVATE crls_server crls_cli ${GUI_LIBS})

# The GUI executable has no console on Windows; this one runs the
# command-line modes (--server, --upload) with one.
if(WIN32)
  add_executable(file-encryptor-cli src/main.cpp ${UI_SRC})
  target_compile_definitions(file-encryptor-cli PRIVATE CRLS_CONSOLE)
  target_link_libraries(file-encryptor-cli PRIVATE crls_server crls_cli ${GUI_LIBS})
endif()

# ---------------- Tests ----------------

enable_testing()

add_library(test_util STATIC tests/test_util.cpp)
target_link_libraries(test_util PUBLIC crls_core)

add_executable(test-archive tests/test_archive.cpp)
target_link_libraries(test-archive PRIVATE test_util)
add_test(NAME archive COMMAND test-archive ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-append tests/test_append.cpp)
target_link_libraries(test-append PRIVATE test_util)
add_test(NAME append COMMAND test-append ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-httpmsg tests/test_httpmsg.cpp)
target_link_libraries(test-httpmsg PRIVATE test_util)
add_test(NAME httpmsg COMMAND test-httpmsg)

find_package(Threads REQUIRED)
add_library(server_fixture STATIC tests/server_fixture.cpp)
target_link_libraries(server_fixture PUBLIC test_util crls_server Threads::Threads)

add_executable(test-server tests/test_server.cpp)
target_link_libraries(test-server PRIVATE server_fixture)
add_test(NAME server COMMAND test-server ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-remote tests/test_remote.cpp)
target_link_libraries(test-remote PRIVATE server_fixture)
add_test(NAME remote COMMAND test-remote)
set_tests_properties(remote PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test-cli tests/test_cli.cpp)
target_link_libraries(test-cli PRIVATE server_fixture crls_cli)
add_test(NAME cli COMMAND test-cli)
set_tests_properties(cli PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test-lib tests/test_lib.cpp)
target_link_libraries(test-lib PRIVATE test_util)
add_test(NAME lib COMMAND test-lib)
