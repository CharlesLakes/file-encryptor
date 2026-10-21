# Test fixtures

## `v2.crls`

An archive written by the **version 2** format code (commit `2dd1da6`), kept so
that newer versions can be checked to still open it.

- Password: `fixture-password`
- Argon2id: libsodium "moderate" limits (opslimit 3, memlimit 256 MiB)
- Live entries, in index order: `empty.bin` (0 bytes, seed 1), `small.txt`
  (1000 bytes, seed 2), `chunks.bin` (3 × 65536 + 5 bytes, seed 3)
- `deleted.bin` (500 bytes, seed 4) was added and then deleted, so the file
  also contains unused data and superseded indexes.
- SHA-256: `635488050bc09eeaa6620514970adb20ef47e4de7a3275938767a8e007fd0b80`

The contents of each entry are produced by `make_file()` in
`tests/test_util.cpp` (xorshift32 seeded with `seed * 2654435761 + 1`), so the
test regenerates the originals and compares them with the extracted files.

Never regenerate this file with newer code: its purpose is to be old.
