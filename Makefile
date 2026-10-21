# FileEncrypter build.
#
#   make          build build/file-encrypter
#   make run      build and start the GUI
#   make test     build and run the core tests (no X11 needed)
#   make clean    remove build/
#
# Only the runtime libraries are needed (libX11.so.6 and libsodium.so.23); their
# declarations live in src/deps/, so no -dev packages are required.

CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-sign-compare
CFLAGS  += -D_DEFAULT_SOURCE -Isrc -MMD -MP
LDLIBS   = -l:libsodium.so.23
GUILIBS  = -l:libX11.so.6

BUILD   = build
BIN     = $(BUILD)/file-encrypter
TEST    = $(BUILD)/test-archive

# Everything except the GUI: shared by the program and the tests.
CORE_SRC = src/core/archive.c src/core/browser.c src/core/status.c src/core/util.c src/lib/vector.c
UI_SRC   = src/ui/actions.c src/ui/draw.c src/ui/input.c src/ui/layout.c src/ui/window.c
APP_SRC  = src/main.c $(UI_SRC) $(CORE_SRC)
TEST_SRC = tests/test_archive.c $(CORE_SRC)

APP_OBJ  = $(APP_SRC:%.c=$(BUILD)/%.o)
TEST_OBJ = $(TEST_SRC:%.c=$(BUILD)/%.o)

.PHONY: all run test clean

all: $(BIN)

$(BIN): $(APP_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS) $(GUILIBS)

$(TEST): $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

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
