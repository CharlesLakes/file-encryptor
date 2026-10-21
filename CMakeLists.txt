# FileEncryptor build.
#
#   cmake -B build                 configure (Release with debug info by default)
#   cmake --build build            build build/file-encryptor
#   ctest --test-dir build         run the tests (no display needed)
#
# The platform is chosen from the target system: Windows uses the Win32 GUI
# backend and the Windows file layer, everything else uses Xlib and POSIX.
#
# On Linux only the runtime libraries are needed (libX11.so.6 and
# libsodium.so.23); their declarations live in src/deps/, so no -dev packages
# are required. On Windows the Win32 API comes with MinGW-w64 and libsodium is
# linked through its import library (libsodium.dll.a).

cmake_minimum_required(VERSION 3.16)
project(FileEncryptor LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# compile_commands.json lets clangd find the include path and flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- Platform ----------------

if(WIN32)
  set(OS_SRC  src/core/fs_win32.c src/core/net_win32.c src/core/http_winhttp.c)
  set(GUI_SRC src/ui/platform_win32.c)
  find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED)
  set(SYSTEM_LIBS ws2_32 winhttp)
  set(GUI_LIBS gdi32 user32)
  set(PLATFORM_DEFS __USE_MINGW_ANSI_STDIO=1)
else()
  set(OS_SRC  src/core/fs_posix.c src/core/net_posix.c src/core/http_curl.c)
  set(GUI_SRC src/ui/platform_x11.c)
  # Link the runtime libraries by their exact file names.
  find_library(SODIUM_LIB NAMES libsodium.so.23 sodium REQUIRED)
  find_library(X11_LIB NAMES libX11.so.6 X11 REQUIRED)
  # libcurl is loaded with dlopen() (see src/deps/curl.h).
  set(SYSTEM_LIBS ${CMAKE_DL_LIBS})
  set(GUI_LIBS ${X11_LIB})
  set(PLATFORM_DEFS "")
endif()

# ---------------- Core (everything except the GUI) ----------------

add_library(crls_core STATIC
  src/core/append.c
  src/core/archive.c
  src/core/browser.c
  src/core/httpmsg.c
  src/core/remote.c
  src/core/source.c
  src/core/status.c
  src/core/util.c
  ${OS_SRC}
  src/lib/vector.c
)
target_include_directories(crls_core PUBLIC src)
target_compile_definitions(crls_core PUBLIC _DEFAULT_SOURCE ${PLATFORM_DEFS})
target_compile_options(crls_core PUBLIC -Wall -Wextra -Wno-sign-compare)
target_link_libraries(crls_core PUBLIC ${SODIUM_LIB} ${SYSTEM_LIBS})

# ---------------- Server ----------------

add_library(crls_server STATIC src/server/server.c)
target_link_libraries(crls_server PUBLIC crls_core)

# ---------------- Program ----------------

set(UI_SRC
  src/ui/actions.c
  src/ui/draw.c
  src/ui/input.c
  src/ui/layout.c
  ${GUI_SRC}
)

add_executable(file-encryptor WIN32 src/main.c ${UI_SRC})
target_link_libraries(file-encryptor PRIVATE crls_server ${GUI_LIBS})

# The GUI executable has no console on Windows; this one runs the
# command-line modes (--server, --upload) with one.
if(WIN32)
  add_executable(file-encryptor-cli src/main.c ${UI_SRC})
  target_compile_definitions(file-encryptor-cli PRIVATE CRLS_CONSOLE)
  target_link_libraries(file-encryptor-cli PRIVATE crls_server ${GUI_LIBS})
endif()

# ---------------- Tests ----------------

enable_testing()

add_library(test_util STATIC tests/test_util.c)
target_link_libraries(test_util PUBLIC crls_core)

add_executable(test-archive tests/test_archive.c)
target_link_libraries(test-archive PRIVATE test_util)
add_test(NAME archive COMMAND test-archive ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-append tests/test_append.c)
target_link_libraries(test-append PRIVATE test_util)
add_test(NAME append COMMAND test-append ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-httpmsg tests/test_httpmsg.c)
target_link_libraries(test-httpmsg PRIVATE test_util)
add_test(NAME httpmsg COMMAND test-httpmsg)

find_package(Threads REQUIRED)
add_library(server_fixture STATIC tests/server_fixture.c)
target_link_libraries(server_fixture PUBLIC test_util crls_server Threads::Threads)

add_executable(test-server tests/test_server.c)
target_link_libraries(test-server PRIVATE server_fixture)
add_test(NAME server COMMAND test-server ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(test-remote tests/test_remote.c)
target_link_libraries(test-remote PRIVATE server_fixture)
add_test(NAME remote COMMAND test-remote)
set_tests_properties(remote PROPERTIES SKIP_RETURN_CODE 77)
