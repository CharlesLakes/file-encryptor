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
 * @section archive_format File Format (version 3)
 *
 * All integers are stored in little-endian byte order.
 *
 * | Offset | Bytes | Content                                         |
 * |--------|-------|-------------------------------------------------|
 * | 0      | 6     | Magic number: "CRLS" followed by two zero bytes |
 * | 6      | 1     | Format version (3)                              |
 * | 7      | 1     | Algorithm (1 = Argon2id + XChaCha20-Poly1305)   |
 * | 8      | 8     | Argon2id operations limit                       |
 * | 16     | 8     | Argon2id memory limit                           |
 * | 24     | 16    | Salt                                            |
 * | 40     | 32    | Ed25519 public key that authorizes uploads      |
 * | 72     | 8     | Offset of the current index                     |
 * | 80     | 8     | Length of the current index                     |
 * | 88     | 8     | Reserved (zeros)                                |
 * | 96     | ...   | File contents and indexes                       |
 *
 * Version 2 archives are still opened and modified. Their header is 64 bytes
 * long, has no public key and keeps the index offset and length at offsets 40
 * and 48. Compacting a version 2 archive converts it to version 3.
 *
 * @section archive_crypto Cryptography
 *
 * A master key is derived from the password and the salt with Argon2id. In
 * version 3 two subkeys are derived from it with crypto_kdf (context
 * "CRLS_v3_"): subkey 1 encrypts the contents and the index, and subkey 2 is
 * the seed of the Ed25519 key pair whose public half is stored in the header.
 * A server can therefore check that an upload was made by someone who knows
 * the password without ever learning it (see append.h). Version 2 uses the
 * master key directly for encryption and has no key pair.
 *
 * Each file is encrypted independently with the secretstream construction
 * (XChaCha20-Poly1305) in chunks of 64 KB. The last chunk carries the final
 * tag, so a truncated file or a file whose chunks were reordered fails to
 * decrypt. Reading or writing a file requires only one chunk in memory.
 *
 * The index consists of a 24-byte nonce followed by the XChaCha20-Poly1305
 * encryption of the structure below, authenticated together with the header
 * up to the index pointer (72 bytes in version 3, 40 in version 2) as
 * associated data:
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
 * only afterwards updates the index pointer in the header. If the program
 * terminates midway, the previous index remains valid. The data of deleted
 * files and superseded indexes remain as unused space until the archive is
 * compacted.
 */

#ifndef CORE_ARCHIVE_H
#define CORE_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <vector>
#include "core/source.h"
#include "lib/unique_fd.h"

namespace crls{

#define MAGIC           "CRLS\0\0"        /**< Magic number at the start of every archive. */
#define MAGIC_BYTES     6                 /**< Length of the magic number. */
#define KEY_BYTES       32                /**< Symmetric key length. */
#define SALT_BYTES      16                /**< Argon2id salt length. */
#define NONCE_BYTES     24                /**< XChaCha20-Poly1305 nonce length. */
#define TAG_BYTES       16                /**< AEAD authentication tag length. */
#define STREAM_HEADER   24                /**< Secretstream header length. */
#define STREAM_ABYTES   17                /**< Secretstream overhead per chunk. */
#define V2_HEADER_BYTES 64                /**< Header length of format version 2. */
#define V2_AD_BYTES     40                /**< Associated data length of version 2. */
#define V3_HEADER_BYTES 96                /**< Header length of format version 3. */
#define V3_AD_BYTES     72                /**< Associated data length of version 3. */
#define MAX_HEADER_BYTES 96               /**< Largest header of any supported version. */
#define MAX_AD_BYTES    72                /**< Largest associated data of any version. */
#define SIGN_PK_BYTES   32                /**< Ed25519 public key length. */
#define SIGN_SK_BYTES   64                /**< Ed25519 secret key length. */
#define SIGN_BYTES      64                /**< Ed25519 signature length. */
#define KDF_CONTEXT     "CRLS_v3_"        /**< crypto_kdf context of version 3. */
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
  bool is_remote;                              /**< True if it was opened from a URL (see remote.h). */
  unique_fd fd;                                /**< Descriptor, held under an exclusive lock (local only). */
  archive_source src;                          /**< Where every read comes from. */
  char path[PATH_MAX];                         /**< Absolute path or URL of the archive. */
  std::vector<archive_entry> entries;          /**< Entries in index order. */
  unsigned char key[KEY_BYTES];                /**< Derived encryption key. */
  unsigned char sign_sk[SIGN_SK_BYTES];        /**< Ed25519 secret key (version 3 only). */
  int version;                                 /**< Format version of the open archive. */
  unsigned int header_bytes;                   /**< Header length of that version. */
  unsigned int ad_len;                         /**< Header bytes used as associated data. */
  unsigned int pointer_pos;                    /**< Offset of the index offset and length. */
  unsigned char header_ad[MAX_AD_BYTES];       /**< Associated data (start of the header). */
  uint64_t index_offset, index_length;         /**< Location of the current index. */
  unsigned char index_nonce[NONCE_BYTES];      /**< Nonce of the current index; identifies its state. */
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
 * @brief  Opens an archive from a byte source already set in a->src (used for
 *         URLs, see remote.h). The archive is read-only through this module:
 *         files are added with signed uploads instead.
 * @param  a        Archive with its source set.
 * @param  name     Name or URL of the archive, kept in a->path.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise.
 */
bool           archive_open_source(archive *a, const char *name, const char *password);

/**
 * @brief  Reads the header and the index again with the keys already derived,
 *         after the archive changed (e.g. another client uploaded a file).
 * @param  a Open archive.
 * @return True on success, false otherwise.
 */
bool           archive_reload(archive *a);

/**
 * @brief  Creates a new, empty archive. Fails if path already exists.
 * @param  a        Archive.
 * @param  path     Path of the new .crls file.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise.
 */
bool           archive_create(archive *a, const char *path, const char *password);

/**
 * @brief  Same as archive_create() with explicit Argon2id limits. Low limits
 *         are only meant for tests.
 * @param  a        Archive.
 * @param  path     Path of the new .crls file.
 * @param  password Password used to derive the key.
 * @param  ops      Argon2id operations limit (1 to MAX_OPSLIMIT).
 * @param  mem      Argon2id memory limit in bytes (8192 to MAX_MEMLIMIT).
 * @return True on success, false otherwise.
 */
bool           archive_create_params(archive *a, const char *path, const char *password,
                                     uint64_t ops, uint64_t mem);

/**
 * @brief  Returns the entry at index. No bounds checking is performed.
 * @param  a     Archive.
 * @param  index Position of the entry, 0 <= index < a->entries.size().
 * @return Pointer to the entry.
 */
archive_entry* archive_get_entry(archive *a, unsigned int index);

/**
 * @brief  Looks up an entry by name.
 * @param  a    Archive.
 * @param  name File name.
 * @return Index of the entry, or -1 if not found.
 */
int            archive_find_entry(archive *a, const char *name);

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
 * @brief  Encrypts the file at path as a new entry named after its basename and
 *         writes its chunks to fd starting at position. The entry is not added
 *         to the index. Used by archive_add_file() and by uploads (append.h).
 * @param  a        Open archive (its key is used).
 * @param  path     Path of the file to encrypt.
 * @param  fd       Destination file descriptor.
 * @param  position Destination offset, which becomes the offset of the entry.
 * @param  e        Output: the new entry.
 * @return True on success, false otherwise (fd may hold partial data).
 */
bool           archive_encrypt_file(archive *a, const char *path, int fd, uint64_t position, archive_entry *e);

/**
 * @brief  Encrypts the current index, optionally with one extra entry at the
 *         end, without writing it anywhere.
 * @param  a         Open archive.
 * @param  extra     Entry appended to the index, or NULL.
 * @param  blob      Output: nonce followed by the ciphertext (free with free()).
 * @param  blob_size Output: length of blob.
 * @return True on success, false otherwise.
 */
bool           archive_build_index(archive *a, const archive_entry *extra, unsigned char **blob, uint64_t *blob_size);

/**
 * @brief  Removes the entry at index from the index. Its data remains as
 *         unused space until archive_compact() is called.
 * @param  a     Archive.
 * @param  index Position of the entry.
 * @return True on success, false otherwise.
 */
bool           archive_delete_entry(archive *a, unsigned int index);

/**
 * @brief  Rewrites the archive keeping only live files. In version 3 the
 *         encrypted chunks are copied as they are, without being decrypted; a
 *         version 2 archive is re-encrypted and converted to version 3.
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

}

#endif
