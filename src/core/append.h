/**
 * @file    append.h
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
 * derived from the password (see archive.h); the server checks the signature
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
 * has to rebuild its index (append_rebase()) on top of the new one.
 *
 * @section append_server Server Side
 *
 * With the archive locked: append_load_state() reads the header and repairs
 * the tail left by an interrupted upload, append_check() validates the claim
 * before any byte of the body is read, and append_commit() streams the body,
 * verifies its hash and only then moves the index pointer. Any failure
 * truncates the file back to the base, so the previous index stays valid.
 */

#ifndef CORE_APPEND_H
#define CORE_APPEND_H

#include <stdbool.h>
#include <stdint.h>
#include "core/archive.h"

namespace crls{

#define APPEND_DOMAIN        "CRLS-APPEND-1"   /**< Domain separation string. */
#define APPEND_DOMAIN_BYTES  14                /**< Its length including the zero byte. */
#define BODY_HASH_BYTES      32                /**< BLAKE2b output length. */
#define APPEND_MESSAGE_BYTES 118               /**< Length of the signed message. */

/** @brief Outcome of a server-side append operation. */
typedef enum{
  APPEND_OK,                 /**< Success. */
  APPEND_BAD_REQUEST,        /**< Malformed claim or body that does not match its hash. */
  APPEND_FORBIDDEN,          /**< Invalid signature: the password is not known. */
  APPEND_CONFLICT,           /**< The archive changed since the client read it. */
  APPEND_TOO_LARGE,          /**< The body exceeds the server limit. */
  APPEND_UNSUPPORTED,        /**< Not a version 3 archive. */
  APPEND_IO                  /**< Read or write error on the server. */
}append_result;

/**
 * @brief What a client asks the server to do, and its signature.
 */
typedef struct{
  uint64_t base;                               /**< End of the current index, where the body goes. */
  unsigned char index_nonce[NONCE_BYTES];      /**< Nonce of the current index. */
  uint64_t content_length;                     /**< Length of the body. */
  uint64_t index_length;                       /**< Length of the new index at the end of the body. */
  unsigned char body_hash[BODY_HASH_BYTES];    /**< BLAKE2b-256 of the body. */
  unsigned char signature[SIGN_BYTES];         /**< Ed25519 signature of the message. */
}append_claim;

/**
 * @brief State of an archive as the server reads it, without any key.
 */
typedef struct{
  unsigned char salt[SALT_BYTES];              /**< Salt from the header. */
  unsigned char public_key[SIGN_PK_BYTES];     /**< Upload key from the header. */
  uint64_t index_offset, index_length;         /**< Location of the current index. */
  unsigned char index_nonce[NONCE_BYTES];      /**< Nonce of the current index. */
}append_state;

/**
 * @brief An upload being prepared by a client.
 */
typedef struct{
  append_claim claim;                          /**< Claim to send with the body. */
  archive_entry entry;                         /**< The new entry, located at claim.base. */
  unsigned char new_nonce[NONCE_BYTES];        /**< Nonce of the new index. */
  int body_fd;                                 /**< Temporary file holding the body. */
}append_upload;

/**
 * @brief Builds the message that a claim signs.
 * @param salt         Salt of the archive.
 * @param index_offset Offset of the current index.
 * @param index_length Length of the current index.
 * @param c            Claim (its signature is not used).
 * @param msg          Output: APPEND_MESSAGE_BYTES bytes.
 */
void          append_message(const unsigned char *salt, uint64_t index_offset, uint64_t index_length,
                             const append_claim *c, unsigned char *msg);

/* ---------------- Client ---------------- */

/**
 * @brief  Encrypts the file at path into a temporary body and signs a claim to
 *         append it to the current state of a.
 * @param  a    Open version 3 archive (local or remote).
 * @param  path File to add.
 * @param  up   Output: the prepared upload; release it with append_discard().
 * @return True on success, false otherwise (the status explains why).
 */
bool          append_prepare(archive *a, const char *path, append_upload *up);

/**
 * @brief  Rebuilds the index part of the body and the claim on top of the
 *         current state of a, after the server answered APPEND_CONFLICT and a
 *         was reloaded. The encrypted file is reused as it is.
 * @param  a  The same archive, reloaded.
 * @param  up Upload to update.
 * @return True on success, false otherwise (e.g. the name now exists).
 */
bool          append_rebase(archive *a, append_upload *up);

/**
 * @brief Updates the in-memory state of a after the server accepted up, so
 *        that it matches the archive on the server.
 * @param a  Archive the upload was prepared for.
 * @param up Accepted upload.
 */
void          append_applied(archive *a, const append_upload *up);

/**
 * @brief Releases the temporary body of an upload.
 * @param up Upload.
 */
void          append_discard(append_upload *up);

/* ---------------- Server ---------------- */

/**
 * @brief Reads exactly len bytes of the body.
 * @param ctx State of the reader.
 * @param buf Destination buffer.
 * @param len Number of bytes.
 * @return True on success, false if the body ended or could not be read.
 */
typedef bool (*append_reader)(void *ctx, void *buf, size_t len);

/**
 * @brief  Reads the state of a locked archive and truncates any bytes after
 *         its current index (left by an interrupted upload).
 * @param  fd Read-write descriptor of the archive, holding its lock.
 * @param  st Output: the state.
 * @return APPEND_OK, APPEND_UNSUPPORTED, APPEND_BAD_REQUEST (damaged header)
 *         or APPEND_IO.
 */
append_result append_load_state(int fd, append_state *st);

/**
 * @brief  Validates a claim against the state, before reading the body.
 * @param  st       State from append_load_state().
 * @param  c        Claim sent by the client.
 * @param  max_body Largest accepted body length.
 * @return APPEND_OK, APPEND_CONFLICT, APPEND_TOO_LARGE, APPEND_BAD_REQUEST or
 *         APPEND_FORBIDDEN.
 */
append_result append_check(const append_state *st, const append_claim *c, uint64_t max_body);

/**
 * @brief  Writes the body at the base while hashing it and, if the hash
 *         matches the claim, points the header to the new index. On any
 *         failure the file is truncated back to the base.
 * @param  fd   Read-write descriptor of the archive, holding its lock.
 * @param  st   State from append_load_state().
 * @param  c    Claim accepted by append_check().
 * @param  read Reader of the body.
 * @param  ctx  State of the reader.
 * @return APPEND_OK, APPEND_BAD_REQUEST (hash mismatch or short body) or
 *         APPEND_IO.
 */
append_result append_commit(int fd, const append_state *st, const append_claim *c,
                            append_reader read, void *ctx);

}

#endif
