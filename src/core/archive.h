/**
 * @file    archive.h
 * @brief   Encrypted .crls archive: open, create, add, extract, delete and compact.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section archive_overview Overview
 *
 * This module is independent of the user interface. Every operation that may
 * fail returns false (or extract_result::failed) and records a descriptive
 * message in the status line (see status.h) before returning; nothing throws.
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "core/source.h"
#include "lib/heap_buffer.h"
#include "lib/secret.h"
#include "lib/unique_fd.h"

namespace crls{

/** @brief Magic number at the start of every archive. */
inline constexpr std::array<unsigned char, 6> MAGIC = {'C', 'R', 'L', 'S', 0, 0};
inline constexpr std::size_t KEY_BYTES = 32;             /**< Symmetric key length. */
inline constexpr std::size_t SALT_BYTES = 16;            /**< Argon2id salt length. */
inline constexpr std::size_t NONCE_BYTES = 24;           /**< XChaCha20-Poly1305 nonce length. */
inline constexpr std::size_t TAG_BYTES = 16;             /**< AEAD authentication tag length. */
inline constexpr std::size_t STREAM_HEADER = 24;         /**< Secretstream header length. */
inline constexpr std::size_t STREAM_ABYTES = 17;         /**< Secretstream overhead per chunk. */
inline constexpr unsigned V2_HEADER_BYTES = 64;          /**< Header length of format version 2. */
inline constexpr unsigned V2_AD_BYTES = 40;              /**< Associated data length of version 2. */
inline constexpr unsigned V3_HEADER_BYTES = 96;          /**< Header length of format version 3. */
inline constexpr unsigned V3_AD_BYTES = 72;              /**< Associated data length of version 3. */
inline constexpr unsigned MAX_HEADER_BYTES = std::max(V2_HEADER_BYTES, V3_HEADER_BYTES); /**< Largest header of any version. */
inline constexpr unsigned MAX_AD_BYTES = std::max(V2_AD_BYTES, V3_AD_BYTES);             /**< Largest associated data of any version. */
inline constexpr std::size_t HEADER_VERSION_POS = 6;     /**< Header offset of the format version. */
inline constexpr std::size_t HEADER_ALGORITHM_POS = 7;   /**< Header offset of the algorithm. */
inline constexpr std::size_t HEADER_OPS_POS = 8;         /**< Header offset of the Argon2id operations limit. */
inline constexpr std::size_t HEADER_MEM_POS = 16;        /**< Header offset of the Argon2id memory limit. */
inline constexpr std::size_t HEADER_SALT_POS = 24;       /**< Header offset of the salt. */
inline constexpr std::size_t HEADER_PK_POS = 40;         /**< Header offset of the Ed25519 public key (version 3). */
inline constexpr std::size_t SIGN_PK_BYTES = 32;         /**< Ed25519 public key length. */
inline constexpr std::size_t SIGN_SK_BYTES = 64;         /**< Ed25519 secret key length. */
inline constexpr std::size_t SIGN_BYTES = 64;            /**< Ed25519 signature length. */
inline constexpr std::string_view KDF_CONTEXT = "CRLS_v3_"; /**< crypto_kdf context of version 3. */
inline constexpr std::size_t CHUNK = 64 * 1024;          /**< Plaintext chunk size. */
inline constexpr std::size_t MAX_NAME = 255;             /**< Maximum length of a stored file name. */
inline constexpr std::size_t MAX_PASSWORD = 128;         /**< Maximum password length. */
inline constexpr std::size_t MIN_PASSWORD = 8;           /**< Minimum password length for new archives. */
inline constexpr uint64_t MAX_INDEX = 64ULL << 20;       /**< Maximum plaintext index size. */
inline constexpr uint64_t MAX_OPSLIMIT = 16;             /**< Maximum accepted Argon2id operations limit. */
inline constexpr uint64_t MAX_MEMLIMIT = 1ULL << 30;     /**< Maximum accepted Argon2id memory limit. */

/**
 * @brief Metadata of a single file stored inside the archive.
 */
struct archive_entry{
  std::string name;                                          /**< File name (basename only). */
  uint64_t size = 0;                                         /**< Plaintext size in bytes. */
  uint64_t offset = 0;                                       /**< Archive offset of the first chunk. */
  uint64_t stored = 0;                                       /**< Encrypted size in bytes. */
  std::array<unsigned char, STREAM_HEADER> stream_header{};  /**< Secretstream header. */
};

/** @brief Outcome of archive::extract(). */
enum class extract_result{
  extracted,                 /**< The file was written. */
  skipped,                   /**< A file with that name already exists in the folder. */
  failed                     /**< An error occurred (the status explains it). */
};

/**
 * @brief A .crls archive, open or closed. Closing (or destroying) it wipes its
 *        keys and releases its file.
 */
class archive{
public:
  archive() = default;
  ~archive(){ close(); }

  archive(const archive &) = delete;
  archive& operator=(const archive &) = delete;

  /**
   * @brief  Opens an existing local archive and decrypts its index.
   * @param  path     Path of the .crls file.
   * @param  password Password used to derive the key.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool open(const std::string &path, const char *password);

  /**
   * @brief  Opens an archive from a byte source (used for URLs, see remote.h).
   *         Files are added to it with signed uploads (see append.h).
   * @param  src      Source of the bytes, owned by the archive from now on.
   * @param  name     Name or URL of the archive, kept as its path.
   * @param  password Password used to derive the key.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool open_source(std::unique_ptr<byte_source> src, const std::string &name, const char *password);

  /**
   * @brief  Reads the header and the index again with the keys already derived,
   *         after the archive changed (e.g. another client uploaded a file).
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool reload();

  /**
   * @brief  Creates a new, empty archive with libsodium's "moderate" Argon2id
   *         limits. Fails if path already exists.
   * @param  path     Path of the new .crls file.
   * @param  password Password used to derive the key.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool create(const std::string &path, const char *password);

  /**
   * @brief  Same as create() with explicit Argon2id limits. Low limits are only
   *         meant for tests.
   * @param  path     Path of the new .crls file.
   * @param  password Password used to derive the key.
   * @param  ops      Argon2id operations limit (1 to MAX_OPSLIMIT).
   * @param  mem      Argon2id memory limit in bytes (8192 to MAX_MEMLIMIT).
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool create(const std::string &path, const char *password, uint64_t ops, uint64_t mem);

  /**
   * @brief Closes the archive, releases its file and wipes its keys. The object
   *        can be opened again afterwards.
   */
  void close();

  /** @brief True while an archive is open. */
  bool is_open() const noexcept{ return open_; }

  /** @brief True if it was opened from a URL (see remote.h). */
  bool is_remote() const noexcept{ return remote_; }

  /** @brief Format version of the open archive. */
  int version() const noexcept{ return version_; }

  /** @brief Header length of that version. */
  unsigned header_bytes() const noexcept{ return header_bytes_; }

  /** @brief Absolute path or URL of the archive. */
  const std::string& path() const noexcept{ return path_; }

  /** @brief Entries in index order. */
  const std::vector<archive_entry>& entries() const noexcept{ return entries_; }

  /**
   * @brief  Looks up an entry by name.
   * @param  name File name.
   * @return Its index, or std::nullopt.
   */
  std::optional<std::size_t> find(std::string_view name) const;

  /** @brief Total plaintext size of all stored files. */
  uint64_t total_size() const;

  /** @brief Space occupied by deleted files and superseded indexes. */
  uint64_t wasted_space() const;

  /** @brief Offset of the current index. */
  uint64_t index_offset() const noexcept{ return index_offset_; }

  /** @brief Length of the current index. */
  uint64_t index_length() const noexcept{ return index_length_; }

  /** @brief Nonce of the current index; identifies the state of the archive. */
  std::span<const unsigned char, NONCE_BYTES> index_nonce() const noexcept{ return index_nonce_; }

  /** @brief The authenticated start of the header (salt at offset 24, public key at 40). */
  std::span<const unsigned char> header_ad() const noexcept{ return {header_ad_.data(), ad_len_}; }

  /** @brief The Argon2id salt from the header. */
  std::span<const unsigned char, SALT_BYTES> salt() const noexcept{
    return std::span(header_ad_).subspan<HEADER_SALT_POS, SALT_BYTES>();
  }

  /** @brief The byte source the archive is read from, or nullptr when closed. */
  byte_source* source() noexcept{ return src_.get(); }

  /** @brief Ed25519 secret key derived from the password (version 3), used to sign uploads. */
  std::span<unsigned char, SIGN_SK_BYTES> signing_key() noexcept{ return sign_sk_.span(); }

  /**
   * @brief  Encrypts the file at path and appends it, named after its basename.
   * @param  path Path of the file to add.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool add_file(const std::string &path);

  /**
   * @brief  Removes an entry from the index. Its data remains as unused space
   *         until compact() is called.
   * @param  index Position of the entry.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool remove(std::size_t index);

  /**
   * @brief  Rewrites the archive keeping only live files. In version 3 the
   *         encrypted chunks are copied as they are, without being decrypted; a
   *         version 2 archive is re-encrypted and converted to version 3.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool compact();

  /**
   * @brief  Decrypts an entry into dir. Existing files are never overwritten.
   * @param  index Position of the entry.
   * @param  dir   Destination directory.
   * @return Whether the file was extracted, skipped or not written.
   */
  extract_result extract(std::size_t index, const std::string &dir);

  /**
   * @brief  Encrypts the file at path as a new entry named after its basename and
   *         writes its chunks to fd starting at position. The entry is not added
   *         to the index. Used by add_file() and by uploads (append.h).
   * @param  path     Path of the file to encrypt.
   * @param  fd       Destination file descriptor.
   * @param  position Destination offset, which becomes the offset of the entry.
   * @return The new entry, or std::nullopt on failure (fd may hold partial data).
   */
  [[nodiscard]] std::optional<archive_entry> encrypt_file(const std::string &path, int fd, uint64_t position);

  /**
   * @brief  Encrypts the current index, optionally with extra entries at the
   *         end, without writing it anywhere.
   * @param  extra Entries appended to the index.
   * @return The nonce followed by the ciphertext, or std::nullopt on failure.
   */
  [[nodiscard]] std::optional<heap_buffer> build_index(std::span<const archive_entry> extra = {});

  /**
   * @brief Records an entry that was appended elsewhere (an accepted upload),
   *        so that the archive matches the copy on the server.
   * @param e            The new entry.
   * @param index_offset Offset of the new index.
   * @param index_length Length of the new index.
   * @param nonce        Nonce of the new index.
   */
  void record_append(const archive_entry &e, uint64_t index_offset, uint64_t index_length,
                     std::span<const unsigned char, NONCE_BYTES> nonce);

private:
  void set_layout(int version);
  [[nodiscard]] bool derive_keys(const char *password, std::span<const unsigned char, MAX_HEADER_BYTES> header,
                                 std::span<unsigned char, SIGN_PK_BYTES> pk);
  void derive_v3_keys(std::span<const unsigned char, KEY_BYTES> master, std::span<unsigned char, SIGN_PK_BYTES> pk);
  [[nodiscard]] bool load(const std::string &name, const char *password);
  [[nodiscard]] bool parse_index(std::span<const unsigned char> plain, uint64_t total_size);
  [[nodiscard]] bool write_index(int fd);
  void remember_path(const std::string &path);

  bool open_ = false;                                    /**< True while an archive is open. */
  bool remote_ = false;                                  /**< True if opened from a URL. */
  unique_fd fd_;                                         /**< Descriptor under an exclusive lock (local only). */
  std::unique_ptr<byte_source> src_;                     /**< Where every read comes from. */
  std::string path_;                                     /**< Absolute path or URL. */
  std::vector<archive_entry> entries_;                   /**< Entries in index order. */
  secret<KEY_BYTES> key_;                                /**< Derived encryption key. */
  secret<SIGN_SK_BYTES> sign_sk_;                        /**< Ed25519 secret key (version 3 only). */
  int version_ = 0;                                      /**< Format version. */
  unsigned header_bytes_ = 0;                            /**< Header length of that version. */
  unsigned ad_len_ = 0;                                  /**< Header bytes used as associated data. */
  unsigned pointer_pos_ = 0;                             /**< Offset of the index offset and length. */
  std::array<unsigned char, MAX_AD_BYTES> header_ad_{};  /**< Associated data (start of the header). */
  uint64_t index_offset_ = 0;                            /**< Offset of the current index. */
  uint64_t index_length_ = 0;                            /**< Length of the current index. */
  std::array<unsigned char, NONCE_BYTES> index_nonce_{}; /**< Nonce of the current index. */

};

/**
 * @brief  Initializes libsodium and verifies that the sizes declared in
 *         deps/sodium.h match those reported by the library. Must be called
 *         once at startup.
 * @return True on success, false otherwise.
 */
bool archive_crypto_init();

/**
 * @brief  Checks whether name ends with the ".crls" extension.
 * @param  name File name.
 * @return True if the extension matches, false otherwise.
 */
bool has_crls_ext(std::string_view name);

}

#endif
