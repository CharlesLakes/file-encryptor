# FileEncrypter

A small desktop app, written in C, that keeps files inside an encrypted,
password-protected archive with the `.crls` extension.

- **Encryption:** Argon2id derives the key from the password. XChaCha20-Poly1305
  encrypts each file in 64 KB chunks and also encrypts the index.
- **GUI:** plain Xlib. There is one window with four screens.
- **No `-dev` packages:** the Xlib and libsodium declarations the code needs are
  written by hand in `src/deps/`, so you only need the runtime libraries.

## Build and run

Requirements: `gcc`, `make`, `libX11.so.6` and `libsodium.so.23`. Both libraries
come with most Linux desktops (on Debian/Ubuntu they are the packages `libx11-6`
and `libsodium23`).

```sh
make          # builds build/file-encrypter
make run      # builds and starts the GUI
make test     # core tests (no X11 needed)
make clean
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
  main.c              entry point: init + X11 event loop
  core/               program logic, no UI code
    archive.h/.c      the .crls format: open, create, add, extract, delete, compact
    browser.h/.c      list of one folder's contents (filtered and sorted)
    status.h/.c       the status line message (set_status)
    util.h/.c         little-endian ints, read_at/write_at, sizes, paths
  ui/                 Xlib GUI
    ui.h              shared state (app, gui), constants, all UI prototypes
    window.c          create/destroy the window and fonts
    layout.c          app state, list geometry, which buttons each screen has
    draw.c            painting of each screen
    actions.c         what each button / Enter does
    input.c           mouse and keyboard to actions
  deps/               hand-written declarations of external libraries
    xlib.h
    sodium.h
  lib/
    vector.h/.c       generic dynamic array (copied from EDDs-in-C)
tests/
  test_archive.c      end-to-end test of core/archive.c
```

Dependencies only go downward: `main → ui → core → lib/deps`. Nothing in
`core/` includes `ui/`, so the core can be tested without a display.

## How it works

### One event, start to finish

```
X11 event ──► input.c ──► actions.c ──► core/archive.c ──► set_status()
  (main.c)    (click/key)  (handle_action)  (does the work)     │
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
