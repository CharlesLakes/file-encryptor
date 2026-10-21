# FileEncrypter build.
#
#   make          build build/file-encrypter (build/file-encrypter.exe on Windows)
#   make run      build and start the GUI
#   make test     build and run the core tests (no display needed)
#   make clean    remove build/
#
# PLATFORM selects the backend: x11 (default) or win32 (default when building
# on Windows, where make sees OS=Windows_NT). Examples:
#
#   make                                               Linux
#   make PLATFORM=win32                                Windows, MSYS2 UCRT64 shell
#   make PLATFORM=win32 CC=x86_64-w64-mingw32-gcc \
#        LDFLAGS=-L/path/to/libsodium-win64/lib       cross-compile from Linux
#
# On Linux only the runtime libraries are needed (libX11.so.6 and
# libsodium.so.23); their declarations live in src/deps/, so no -dev packages
# are required. On Windows the Win32 API comes with MinGW-w64 and libsodium is
# linked through its import library (libsodium.dll.a).

ifeq ($(OS),Windows_NT)
  PLATFORM ?= win32
else
  PLATFORM ?= x11
endif

CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-sign-compare
CFLAGS  += -D_DEFAULT_SOURCE -Isrc -MMD -MP

ifeq ($(PLATFORM),win32)
  EXE      = .exe
  CFLAGS  += -D__USE_MINGW_ANSI_STDIO=1
  LDLIBS   = -lsodium
  GUILIBS  = -lgdi32 -luser32 -mwindows
  FS_SRC   = src/core/fs_win32.c
  GUI_SRC  = src/ui/platform_win32.c
else
  EXE      =
  LDLIBS   = -l:libsodium.so.23
  GUILIBS  = -l:libX11.so.6
  FS_SRC   = src/core/fs_posix.c
  GUI_SRC  = src/ui/platform_x11.c
endif

BUILD   = build
BIN     = $(BUILD)/file-encrypter$(EXE)
TEST    = $(BUILD)/test-archive$(EXE)

# Everything except the GUI: shared by the program and the tests.
CORE_SRC = src/core/archive.c src/core/browser.c src/core/status.c src/core/util.c $(FS_SRC) src/lib/vector.c
UI_SRC   = src/ui/actions.c src/ui/draw.c src/ui/input.c src/ui/layout.c $(GUI_SRC)
APP_SRC  = src/main.c $(UI_SRC) $(CORE_SRC)
TEST_SRC = tests/test_archive.c $(CORE_SRC)

APP_OBJ  = $(APP_SRC:%.c=$(BUILD)/%.o)
TEST_OBJ = $(TEST_SRC:%.c=$(BUILD)/%.o)

.PHONY: all run test clean

all: $(BIN)

$(BIN): $(APP_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS) $(GUILIBS)

$(TEST): $(TEST_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

test: $(TEST)
	./$(TEST)

clean:
	rm -rf $(BUILD)

-include $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
