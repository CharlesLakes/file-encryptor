/**
 * @file    archive.h
 * @brief   Encrypted .crls archive: open, create, add, extract, delete and compact.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section archive_overview Overview
 *
 * This module is independent of the user interface. Every function that may
 * fail returns false (or -1) and records a descriptive message in the status
 * line (see status.h) before returning.
 *
 * @section archive_format File Format (version 2)
 *
 * All integers are stored in little-endian byte order.
 *
 * | Offset | Bytes | Content                                         |
 * |--------|-------|-------------------------------------------------|
 * | 0      | 6     | Magic number: "CRLS" followed by two zero bytes |
 * | 6      | 1     | Format version (2)                              |
 * | 7      | 1     | Algorithm (1 = Argon2id + XChaCha20-Poly1305)   |
 * | 8      | 8     | Argon2id operations limit                       |
 * | 16     | 8     | Argon2id memory limit                           |
 * | 24     | 16    | Salt                                            |
 * | 40     | 8     | Offset of the current index                     |
 * | 48     | 8     | Length of the current index                     |
 * | 56     | 8     | Reserved (zeros)                                |
 * | 64     | ...   | File contents and indexes                       |
 *
 * @section archive_crypto Cryptography
 *
 * The encryption key is derived from the password and the salt with Argon2id.
 *
 * Each file is encrypted independently with the secretstream construction
 * (XChaCha20-Poly1305) in chunks of 64 KB. The last chunk carries the final
 * tag, so a truncated file or a file whose chunks were reordered fails to
 * decrypt. Reading or writing a file requires only one chunk in memory.
 *
 * The index consists of a 24-byte nonce followed by the XChaCha20-Poly1305
 * encryption of the structure below, authenticated together with the first
 * 40 header bytes as associated data:
 *
 * @code
 * u32 number of files
 * per file: u16 name length, name, u64 size, u64 offset,
 *           u64 stored bytes, 24-byte secretstream header
 * @endcode
 *
 * @section archive_consistency Crash Consistency
 *
 * Adding or deleting a file appends a new index at the end of the archive and
 * only afterwards updates the index offset in the header. If the program
 * terminates midway, the previous index remains valid. The data of deleted
 * files and superseded indexes remain as unused space until the archive is
 * compacted.
 */

#ifndef CORE_ARCHIVE_H
#define CORE_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include "lib/vector.h"

#define MAGIC           "CRLS\0\0"        /**< Magic number at the start of every archive. */
#define MAGIC_BYTES     6                 /**< Length of the magic number. */
#define KEY_BYTES       32                /**< Symmetric key length. */
#define SALT_BYTES      16                /**< Argon2id salt length. */
#define NONCE_BYTES     24                /**< XChaCha20-Poly1305 nonce length. */
#define TAG_BYTES       16                /**< AEAD authentication tag length. */
#define STREAM_HEADER   24                /**< Secretstream header length. */
#define STREAM_ABYTES   17                /**< Secretstream overhead per chunk. */
#define HEADER_BYTES    64                /**< Total archive header length. */
#define HEADER_AD_BYTES 40                /**< Header bytes used as associated data. */
#define CHUNK           (64 * 1024)       /**< Plaintext chunk size. */
#define MAX_NAME        255               /**< Maximum length of a stored file name. */
#define MAX_PASSWORD    128               /**< Maximum password length. */
#define MIN_PASSWORD    8                 /**< Minimum password length for new archives. */
#define MAX_INDEX       (64ULL << 20)     /**< Maximum plaintext index size. */
#define MAX_OPSLIMIT    16ULL             /**< Maximum accepted Argon2id operations limit. */
#define MAX_MEMLIMIT    (1ULL << 30)      /**< Maximum accepted Argon2id memory limit. */

/**
 * @brief Metadata of a single file stored inside the archive.
 */
typedef struct{
  char name[MAX_NAME + 1];                     /**< File name (basename only). */
  uint64_t size;                               /**< Plaintext size in bytes. */
  uint64_t offset;                             /**< Archive offset of the first chunk. */
  uint64_t stored;                             /**< Encrypted size in bytes. */
  unsigned char stream_header[STREAM_HEADER];  /**< Secretstream header. */
}archive_entry;

/**
 * @brief State of a .crls archive, either open or closed.
 */
typedef struct{
  bool is_open;                                /**< True while an archive is open. */
  int fd;                                      /**< File descriptor, held under an exclusive flock. */
  char path[PATH_MAX];                         /**< Absolute path of the archive. */
  vector entries;                              /**< archive_entry elements, in index order. */
  unsigned char key[KEY_BYTES];                /**< Derived encryption key. */
  unsigned char header_ad[HEADER_AD_BYTES];    /**< First 40 header bytes (associated data). */
  uint64_t index_offset, index_length;         /**< Location of the current index. */
}archive;

/**
 * @brief  Initializes libsodium and verifies that the sizes declared above
 *         match those reported by the library. Must be called once at startup.
 * @return True on success, false otherwise.
 */
bool           archive_crypto_init(void);

/**
 * @brief Initializes an archive structure. Must be called before first use.
 * @param a Archive.
 */
void           archive_init(archive *a);

/**
 * @brief Closes the archive, releases its file descriptor and wipes the key.
 *        The structure may be reused afterwards.
 * @param a Archive.
 */
void           archive_close(archive *a);

/**
 * @brief Closes the archive and releases all memory it owns.
 * @param a Archive.
 */
void           archive_free(archive *a);

/**
 * @brief  Opens an existing archive and decrypts its index.
 * @param  a        Archive.
 * @param  path     Path of the .crls file.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise.
 */
bool           archive_open(archive *a, const char *path, const char *password);

/**
 * @brief  Creates a new, empty archive. Fails if path already exists.
 * @param  a        Archive.
 * @param  path     Path of the new .crls file.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise.
 */
bool           archive_create(archive *a, const char *path, const char *password);

/**
 * @brief  Returns the entry at index. No bounds checking is performed.
 * @param  a     Archive.
 * @param  index Position of the entry, 0 <= index < a->entries.n.
 * @return Pointer to the entry.
 */
archive_entry* archive_get_entry(archive *a, unsigned int index);

/**
 * @brief  Computes the total plaintext size of all stored files.
 * @param  a Archive.
 * @return Size in bytes.
 */
uint64_t       archive_total_size(archive *a);

/**
 * @brief  Computes the space occupied by deleted files and superseded indexes.
 * @param  a Archive.
 * @return Unused space in bytes.
 */
uint64_t       archive_wasted_space(archive *a);

/**
 * @brief  Encrypts the file at path and appends it to the archive under its
 *         basename.
 * @param  a    Archive.
 * @param  path Path of the file to add.
 * @return True on success, false otherwise.
 */
bool           archive_add_file(archive *a, const char *path);

/**
 * @brief  Removes the entry at index from the index. Its data remains as
 *         unused space until archive_compact() is called.
 * @param  a     Archive.
 * @param  index Position of the entry.
 * @return True on success, false otherwise.
 */
bool           archive_delete_entry(archive *a, unsigned int index);

/**
 * @brief  Rewrites the archive keeping only live files. Encrypted chunks are
 *         copied as they are, without being decrypted.
 * @param  a Archive.
 * @return True on success, false otherwise.
 */
bool           archive_compact(archive *a);

/**
 * @brief  Decrypts the entry at index into dir. Existing files are never
 *         overwritten.
 * @param  a     Archive.
 * @param  index Position of the entry.
 * @param  dir   Destination directory.
 * @return 1 if the file was extracted, 0 if a file with the same name already
 *         exists in dir, -1 on error.
 */
int            archive_extract_entry(archive *a, unsigned int index, const char *dir);

/**
 * @brief  Checks whether name ends with the ".crls" extension.
 * @param  name File name.
 * @return True if the extension matches, false otherwise.
 */
bool           has_crls_ext(const char *name);

#endif
