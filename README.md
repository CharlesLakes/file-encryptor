# FileEncrypter

A small desktop app, written in C, that keeps files inside an encrypted,
password-protected archive with the `.crls` extension.

- **Encryption:** Argon2id derives the key from the password. XChaCha20-Poly1305
  encrypts each file in 64 KB chunks and also encrypts the index.
- **GUI:** one window with four screens, drawn with plain Xlib on Linux and
  with Win32/GDI on Windows.
- **No `-dev` packages on Linux:** the Xlib and libsodium declarations the code
  needs are written by hand in `src/deps/`, so you only need the runtime
  libraries.

## Build and run

### Linux

Requirements: `gcc`, `make`, `libX11.so.6` and `libsodium.so.23`. Both libraries
come with most Linux desktops (on Debian/Ubuntu they are the packages `libx11-6`
and `libsodium23`).

```sh
make          # builds build/file-encrypter
make run      # builds and starts the GUI
make test     # core tests (no display needed)
make clean
```

### Windows

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** shell and run:

```sh
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libsodium
make          # builds build/file-encrypter.exe (PLATFORM=win32 is picked automatically)
make run
make test
```

To run the program outside the MSYS2 shell, copy `libsodium-26.dll` from
`C:\msys64\ucrt64\bin` next to `file-encrypter.exe`.

To cross-compile from Linux with MinGW-w64, use the
[prebuilt libsodium for MinGW](https://download.libsodium.org/libsodium/releases/):

```sh
make PLATFORM=win32 CC=x86_64-w64-mingw32-gcc LDFLAGS=-L/path/to/libsodium-win64/lib
```

## Using it

1. **Open screen:** pick a `.crls` file in the list or type its path, then type
   the password and press **Open**. To make a new archive, type a name, fill in
   both password fields (at least 8 characters) and press **Create new**.
2. **Archive screen:** **Add** and **Extract** open a folder browser.
   **Delete** must be pressed twice to confirm. Deleted data stays in the file as
   unused space until you press **Compact**.
3. Keys: arrow keys, PgUp/PgDn and Home/End move the selection. Enter runs the
   selected item, Esc cancels, Backspace goes up one folder, Tab switches
   between fields, Delete removes the selected file.

Extraction never overwrites anything: a file that already exists in the target
folder is skipped.

## Repository layout

```
Makefile
compile_flags.txt     compiler flags for clangd (editor/LSP), keep in sync with Makefile
src/
  main.c              entry point: init + event loop
  core/               program logic, no UI code
    archive.h/.c      the .crls format: open, create, add, extract, delete, compact
    browser.h/.c      list of one folder's contents (filtered and sorted)
    status.h/.c       the status line message (set_status)
    util.h/.c         little-endian ints, sizes, paths
    fs.h              everything that differs between operating systems
    fs_posix.c        ... for Linux (pread, flock, realpath, rename)
    fs_win32.c        ... for Windows (ReadFile, LockFileEx, MoveFileEx)
  ui/                 GUI
    ui.h              shared state (app), constants, all UI prototypes
    platform.h        window, events, drawing and fonts, independent of the OS
    platform_x11.c    ... implemented with Xlib
    platform_win32.c  ... implemented with Win32/GDI
    layout.c          app state, list geometry, which buttons each screen has
    draw.c            painting of each screen
    actions.c         what each button / Enter does
    input.c           mouse and keyboard to actions
  deps/               hand-written declarations of external libraries
    xlib.h            used only by platform_x11.c
    sodium.h
  lib/
    vector.h/.c       generic dynamic array (copied from EDDs-in-C)
tests/
  test_archive.c      end-to-end test of core/archive.c
```

Dependencies only go downward: `main → ui → core → lib/deps`. Nothing in
`core/` includes `ui/`, so the core can be tested without a display.

The Makefile variable `PLATFORM` (`x11` or `win32`) picks one `fs_*.c` and one
`platform_*.c`. No other file contains operating system specific code, except
for a few `#ifdef _WIN32` lines in `tests/test_archive.c`.

## How it works

### One event, start to finish

```
platform event ──► input.c ──► actions.c ──► core/archive.c ──► set_status()
  (main.c)         (click/key)  (handle_action)  (does the work)     │
                                                                     ▼
                                            draw.c repaints the whole window
```

All UI state lives in a single global `app` (`app_state` in `ui/ui.h`). The
current screen is `app.screen`. Functions that can fail return `false` (or
`-1`) after they have already set an error message with `set_status()`, so the
caller only checks the return value.

### The `.crls` file format

See the comment at the top of `src/core/archive.h` for the byte-level layout.
In short:

```
[64 byte header][file 1 chunks][file 2 chunks]...[index][file 3 chunks][index]...
                                                         ▲ header points to the latest index
```

- The header holds the Argon2id parameters, the salt and the position of the
  current index.
- Each file is a separate secretstream, so a truncated or reordered file fails
  to decrypt.
- The index (names, sizes, offsets) is encrypted and authenticated together
  with the header.
- Add and Delete append a **new** index and then update the header pointer. If
  the program stops halfway, the previous index still works.
- **Compact** copies the live encrypted chunks into `<name>.tmp` without
  decrypting them, writes a fresh index and renames the temp file over the
  original.

Every `.crls` file starts with the magic bytes `CRLS` followed by two zero
bytes.

## Contributing (humans and LLMs)

- **Where things go:** format or crypto changes go in `core/archive.c`. A new
  button means an `ACT_*` in `ui/ui.h`, an entry in `get_buttons()`
  (`layout.c`) and a `case` in `handle_action()` (`actions.c`). Painting goes
  in `draw.c`.
- **Using a new libsodium or Xlib function:** declare it in `src/deps/` with the
  exact signature from the real header.
- **Operating system specific code:** file and path operations go in
  `core/fs.h` with one implementation in each `fs_*.c`; windowing goes in
  `ui/platform.h` with one implementation in each `platform_*.c`. Always add
  `O_BINARY` to `open()` flags, and use `fs_basename()` / `fs_is_separator()`
  instead of looking for `'/'`.
- **Style:** C11, two-space indentation, `snake_case`, `static` for anything
  private to a file. Every file starts with a Doxygen block (`@file`, `@brief`,
  `@author`, `Maintainer:`) and public functions are documented in their
  header with `@brief`, `@param` and `@return`, following `src/lib/vector.h`.
  All code, comments and UI text are in English.
- **Secrets:** wipe passwords, keys and plaintext buffers with
  `sodium_memzero()` when you are done with them.
- **Editor setup:** clangd (LunarVim, VS Code, etc.) reads `compile_flags.txt`
  to find the `src/` include path. If you change `CFLAGS` in the Makefile,
  update that file too.
- **Before committing:** `make && make test` must finish with no warnings, and
  `make test` must print `All tests passed`. If you change the format, add a
  test in `tests/test_archive.c`.
- **Commit messages:** follow
  [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):
  `<type>[optional scope]: <description>`, e.g. `feat(ui): add Windows backend`
  or `fix(core): reject backslashes in stored names`. Common types are `feat`,
  `fix`, `docs`, `refactor`, `test`, `build` and `chore`; a breaking change adds
  `!` before the colon.
  - Write only the title line, with no body.
  - `Co-authored-by` lines for AI tools are **forbidden**. When an AI assistant
    helped, add a single `Assisted-by` trailer after a blank line, as described
    in the
    [Linux kernel guidelines](https://docs.kernel.org/process/coding-assistants.html):
    `Assisted-by: LLM [TOOL1] [TOOL2]`, where the tools are optional
    specialized analysis tools (not git, gcc, make or editors).
