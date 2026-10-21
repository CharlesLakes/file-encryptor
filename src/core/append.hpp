/**
 * @file    append.hpp
 * @brief   Signed appends: adding a file to an archive held by a server that
 *          does not know the password.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section append_overview Overview
 *
 * The client encrypts the new file and builds the new index itself, exactly as
 * archive_add_file() would, and sends only these bytes (the body):
 *
 * @code
 * [chunks of the new entry][new encrypted index]
 * @endcode
 *
 * The body is meant to be written at the current end of the archive, which is
 * always the end of the current index (the base). The client signs a
 * description of the state it saw and of the body with the Ed25519 secret key
 * derived from the password (see archive.hpp); the server checks the signature
 * with the public key stored in the header, so it never needs the password or
 * any secret. Without the password there is no valid signature, and the server
 * rejects the upload.
 *
 * @section append_message Signed Message
 *
 * | Bytes | Content                                    |
 * |-------|--------------------------------------------|
 * | 14    | "CRLS-APPEND-1" followed by a zero byte    |
 * | 16    | Salt of the archive                        |
 * | 8     | Offset of the current index                |
 * | 8     | Length of the current index                |
 * | 24    | Nonce of the current index                 |
 * | 8     | Length of the body                         |
 * | 8     | Length of the new index (end of the body)  |
 * | 32    | BLAKE2b-256 of the body                    |
 *
 * Each index is encrypted with a fresh random nonce, so the nonce identifies
 * the state of the archive: a signature cannot be replayed once the archive
 * has changed, and a client that saw an older state gets APPEND_CONFLICT and
 * has to rebuild its index (append_upload::rebase()) on top of the new one.
 *
 * @section append_server Server Side
 *
 * With the archive locked: append_load_state() reads the header and repairs
 * the tail left by an interrupted upload, append_check() validates the claim
 * before any byte of the body is read, and append_commit() streams the body,
 * verifies its hash and only then moves the index pointer. Any failure
 * truncates the file back to the base, so the previous index stays valid.
 */

#ifndef CORE_APPEND_HPP
#define CORE_APPEND_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include "core/archive.hpp"
#include "lib/function_ref.hpp"
#include "lib/unique_fd.hpp"

namespace crls{

inline constexpr char APPEND_DOMAIN[] = "CRLS-APPEND-1";  /**< Domain separation string. */
inline constexpr std::size_t APPEND_DOMAIN_BYTES = 14;    /**< Its length including the zero byte. */
inline constexpr std::size_t BODY_HASH_BYTES = 32;        /**< BLAKE2b output length. */
inline constexpr std::size_t APPEND_MESSAGE_BYTES = 118;  /**< Length of the signed message. */

static_assert(sizeof(APPEND_DOMAIN) == APPEND_DOMAIN_BYTES, "the domain is signed with its zero byte");

/** @brief Outcome of a server-side append operation. */
enum class append_result{
  ok,                        /**< Success. */
  bad_request,               /**< Malformed claim or body that does not match its hash. */
  forbidden,                 /**< Invalid signature: the password is not known. */
  conflict,                  /**< The archive changed since the client read it. */
  too_large,                 /**< The body exceeds the server limit. */
  unsupported,               /**< Not a version 3 archive. */
  io                         /**< Read or write error on the server. */
};

/**
 * @brief What a client asks the server to do, and its signature.
 */
struct append_claim{
  uint64_t base = 0;                                    /**< End of the current index, where the body goes. */
  std::array<unsigned char, NONCE_BYTES> index_nonce{}; /**< Nonce of the current index. */
  uint64_t content_length = 0;                          /**< Length of the body. */
  uint64_t index_length = 0;                            /**< Length of the new index at the end of the body. */
  std::array<unsigned char, BODY_HASH_BYTES> body_hash{}; /**< BLAKE2b-256 of the body. */
  std::array<unsigned char, SIGN_BYTES> signature{};    /**< Ed25519 signature of the message. */
};

/**
 * @brief State of an archive as the server reads it, without any key.
 */
struct append_state{
  std::array<unsigned char, SALT_BYTES> salt{};         /**< Salt from the header. */
  std::array<unsigned char, SIGN_PK_BYTES> public_key{};/**< Upload key from the header. */
  uint64_t index_offset = 0;                            /**< Offset of the current index. */
  uint64_t index_length = 0;                            /**< Length of the current index. */
  std::array<unsigned char, NONCE_BYTES> index_nonce{}; /**< Nonce of the current index. */
};

/**
 * @brief  Builds the message that a claim signs.
 * @param  salt         Salt of the archive.
 * @param  index_offset Offset of the current index.
 * @param  index_length Length of the current index.
 * @param  c            Claim (its signature is not used).
 * @return The message.
 */
std::array<unsigned char, APPEND_MESSAGE_BYTES> append_message(std::span<const unsigned char, SALT_BYTES> salt,
                                                               uint64_t index_offset, uint64_t index_length,
                                                               const append_claim &c);

/* ---------------- Client ---------------- */

/**
 * @brief An upload being prepared by a client. Owns the temporary file that
 *        holds the body, which is deleted with the object.
 */
class append_upload{
public:
  /**
   * @brief  Encrypts the file at path into a temporary body and signs a claim
   *         to append it to the current state of a.
   * @param  a    Open version 3 archive (local or remote).
   * @param  path File to add.
   * @return True on success, false otherwise (the status explains why).
   */
  [[nodiscard]] bool prepare(archive &a, const std::string &path);

  /**
   * @brief  Rebuilds the index part of the body and the claim on top of the
   *         current state of a, after the server answered
   *         append_result::conflict and a was reloaded. The encrypted file is
   *         reused as it is.
   * @param  a The same archive, reloaded.
   * @return True on success, false otherwise (e.g. the name now exists).
   */
  [[nodiscard]] bool rebase(archive &a);

  /**
   * @brief Updates the in-memory state of a after the server accepted the
   *        upload, so that it matches the archive on the server.
   * @param a Archive the upload was prepared for.
   */
  void applied(archive &a) const;

  /** @brief The new entry, located at claim.base. */
  const archive_entry& entry() const noexcept{ return entry_; }

  /** @brief Descriptor of the temporary file holding the body. */
  int body_fd() const noexcept{ return body_.get(); }

  append_claim claim;                                   /**< Claim to send with the body. */

private:
  archive_entry entry_;
  std::array<unsigned char, NONCE_BYTES> new_nonce_{};
  unique_fd body_;
};

/* ---------------- Server ---------------- */

/**
 * @brief Reads exactly buf.size() bytes of the body; returns false if the
 *        body ended or could not be read.
 */
using append_reader = function_ref<bool(std::span<unsigned char> buf)>;

/**
 * @brief  Reads the state of a locked archive and truncates any bytes after
 *         its current index (left by an interrupted upload).
 * @param  fd Read-write descriptor of the archive, holding its lock.
 * @param  st Output: the state.
 * @return ok, unsupported, bad_request (damaged header) or io.
 */
append_result append_load_state(int fd, append_state &st);

/**
 * @brief  Validates a claim against the state, before reading the body.
 * @param  st       State from append_load_state().
 * @param  c        Claim sent by the client.
 * @param  max_body Largest accepted body length.
 * @return ok, conflict, too_large, bad_request or forbidden.
 */
append_result append_check(const append_state &st, const append_claim &c, uint64_t max_body);

/**
 * @brief  Writes the body at the base while hashing it and, if the hash
 *         matches the claim, points the header to the new index. On any
 *         failure the file is truncated back to the base.
 * @param  fd   Read-write descriptor of the archive, holding its lock.
 * @param  c    Claim accepted by append_check().
 * @param  read Reader of the body.
 * @return ok, bad_request (hash mismatch or short body) or io.
 */
append_result append_commit(int fd, const append_claim &c, append_reader read);

}

#endif
