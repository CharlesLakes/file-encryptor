/**
 * @file    append.cpp
 * @brief   Implementation of signed appends (see append.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/append.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

namespace crls{

/** @brief Scratch buffer used to stream bodies. */
static unsigned char body_chunk[CHUNK];

void append_message(const unsigned char *salt, uint64_t index_offset, uint64_t index_length,
                    const append_claim *c, unsigned char *msg){
  unsigned char *p = msg;
  memcpy(p, APPEND_DOMAIN, APPEND_DOMAIN_BYTES);
  p += APPEND_DOMAIN_BYTES;
  memcpy(p, salt, SALT_BYTES);
  p += SALT_BYTES;
  put_le(p, index_offset, 8);
  put_le(p + 8, index_length, 8);
  p += 16;
  memcpy(p, c->index_nonce, NONCE_BYTES);
  p += NONCE_BYTES;
  put_le(p, c->content_length, 8);
  put_le(p + 8, c->index_length, 8);
  p += 16;
  memcpy(p, c->body_hash, BODY_HASH_BYTES);
  p += BODY_HASH_BYTES;
  static_assert(APPEND_DOMAIN_BYTES + SALT_BYTES + 16 + NONCE_BYTES + 16 + BODY_HASH_BYTES == APPEND_MESSAGE_BYTES,
                 "APPEND_MESSAGE_BYTES must match the fields of the message");
}

/**
 * @brief  Computes the BLAKE2b-256 hash of the first len bytes of a file.
 * @param  fd   File descriptor.
 * @param  len  Number of bytes.
 * @param  hash Output: BODY_HASH_BYTES bytes.
 * @return True on success, false on a read error.
 */
static bool hash_file(int fd, uint64_t len, unsigned char *hash){
  crypto_generichash_state state;
  crypto_generichash_init(&state, NULL, 0, BODY_HASH_BYTES);
  for(uint64_t done = 0; done < len; ){
    size_t n = len - done < sizeof(body_chunk) ? (size_t) (len - done) : sizeof(body_chunk);
    if(!read_at(fd, body_chunk, n, done)) return false;
    crypto_generichash_update(&state, body_chunk, n);
    done += n;
  }
  crypto_generichash_final(&state, hash, BODY_HASH_BYTES);
  return true;
}

/* ---------------- Client ---------------- */

bool append_prepare(archive *a, const char *path, append_upload *up){
  memset(up, 0, sizeof(*up));
  up->body_fd = -1;
  if(a->version < 3){
    set_status(true, "Uploads need a version 3 archive. Compact it first.");
    return false;
  }
  up->body_fd = fs_temp_file();
  if(up->body_fd < 0){
    set_status(true, "Could not create a temporary file.");
    return false;
  }
  /* The ciphertext does not depend on where it is stored, so it is written at
     offset 0 of the body and only the index records its final offset. */
  if(!archive_encrypt_file(a, path, up->body_fd, 0, &up->entry) || !append_rebase(a, up)){
    append_discard(up);
    return false;
  }
  return true;
}

bool append_rebase(archive *a, append_upload *up){
  if(archive_find_entry(a, up->entry.name) >= 0){
    set_status(true, "There is already a file named %s in the .crls.", up->entry.name);
    return false;
  }
  append_claim *c = &up->claim;
  c->base = a->index_offset + a->index_length;
  memcpy(c->index_nonce, a->index_nonce, NONCE_BYTES);
  up->entry.offset = c->base;

  unsigned char *blob;
  uint64_t blob_size;
  if(!archive_build_index(a, &up->entry, &blob, &blob_size)) return false;
  bool ok = write_at(up->body_fd, blob, (size_t) blob_size, up->entry.stored) &&
            fs_truncate(up->body_fd, up->entry.stored + blob_size);
  memcpy(up->new_nonce, blob, NONCE_BYTES);
  free(blob);

  c->content_length = up->entry.stored + blob_size;
  c->index_length = blob_size;
  ok = ok && hash_file(up->body_fd, c->content_length, c->body_hash);
  if(!ok){
    set_status(true, "Could not write the temporary file.");
    return false;
  }

  unsigned char msg[APPEND_MESSAGE_BYTES];
  append_message(a->header_ad + 24, a->index_offset, a->index_length, c, msg);
  crypto_sign_detached(c->signature, NULL, msg, sizeof(msg), a->sign_sk);
  return true;
}

void append_applied(archive *a, const append_upload *up){
  a->entries.push_back(up->entry);
  a->index_offset = up->claim.base + up->entry.stored;
  a->index_length = up->claim.index_length;
  memcpy(a->index_nonce, up->new_nonce, NONCE_BYTES);
}

void append_discard(append_upload *up){
  if(up->body_fd >= 0) close(up->body_fd);
  up->body_fd = -1;
}

/* ---------------- Server ---------------- */

append_result append_load_state(int fd, append_state *st){
  unsigned char header[V3_HEADER_BYTES];
  uint64_t total = file_size(fd);
  if(total < V3_HEADER_BYTES || !read_at(fd, header, V3_HEADER_BYTES, 0) ||
     memcmp(header, MAGIC, MAGIC_BYTES) != 0) return APPEND_BAD_REQUEST;
  /* In version 2, bytes 40 to 56 are the index pointer, not a key. */
  if(header[6] != 3 || header[7] != 1) return APPEND_UNSUPPORTED;

  memcpy(st->salt, header + 24, SALT_BYTES);
  memcpy(st->public_key, header + 40, SIGN_PK_BYTES);
  st->index_offset = get_le(header + V3_AD_BYTES, 8);
  st->index_length = get_le(header + V3_AD_BYTES + 8, 8);
  if(st->index_offset < V3_HEADER_BYTES || st->index_offset > total ||
     st->index_length < NONCE_BYTES + TAG_BYTES + 4 || st->index_length > total - st->index_offset)
    return APPEND_BAD_REQUEST;
  if(!read_at(fd, st->index_nonce, NONCE_BYTES, st->index_offset)) return APPEND_IO;

  /* Every committed write ends with the current index; anything after it is
     the remainder of an interrupted upload. */
  uint64_t end = st->index_offset + st->index_length;
  if(total > end && !fs_truncate(fd, end)) return APPEND_IO;
  return APPEND_OK;
}

append_result append_check(const append_state *st, const append_claim *c, uint64_t max_body){
  if(c->base != st->index_offset + st->index_length ||
     sodium_memcmp(c->index_nonce, st->index_nonce, NONCE_BYTES) != 0) return APPEND_CONFLICT;
  if(c->content_length > max_body) return APPEND_TOO_LARGE;
  if(c->index_length < NONCE_BYTES + TAG_BYTES + 4 || c->index_length > NONCE_BYTES + MAX_INDEX + TAG_BYTES ||
     c->index_length > c->content_length) return APPEND_BAD_REQUEST;

  unsigned char msg[APPEND_MESSAGE_BYTES];
  append_message(st->salt, st->index_offset, st->index_length, c, msg);
  if(crypto_sign_verify_detached(c->signature, msg, sizeof(msg), st->public_key) != 0) return APPEND_FORBIDDEN;
  return APPEND_OK;
}

append_result append_commit(int fd, const append_state *st, const append_claim *c,
                            append_reader read, void *ctx){
  (void) st;
  crypto_generichash_state state;
  crypto_generichash_init(&state, NULL, 0, BODY_HASH_BYTES);
  append_result result = APPEND_OK;

  for(uint64_t done = 0; result == APPEND_OK && done < c->content_length; ){
    size_t n = c->content_length - done < sizeof(body_chunk) ? (size_t) (c->content_length - done) : sizeof(body_chunk);
    if(!read(ctx, body_chunk, n)) result = APPEND_BAD_REQUEST;
    else if(!write_at(fd, body_chunk, n, c->base + done)) result = APPEND_IO;
    else crypto_generichash_update(&state, body_chunk, n);
    done += n;
  }

  unsigned char hash[BODY_HASH_BYTES];
  crypto_generichash_final(&state, hash, BODY_HASH_BYTES);
  if(result == APPEND_OK && sodium_memcmp(hash, c->body_hash, BODY_HASH_BYTES) != 0) result = APPEND_BAD_REQUEST;

  bool pointer_written = false;
  if(result == APPEND_OK){
    unsigned char pointer[16];
    put_le(pointer, c->base + c->content_length - c->index_length, 8);
    put_le(pointer + 8, c->index_length, 8);
    /* The body must be on disk before the header points to it. */
    if(!fs_sync(fd) || !(pointer_written = write_at(fd, pointer, sizeof(pointer), V3_AD_BYTES)) || !fs_sync(fd))
      result = APPEND_IO;
  }
  /* Once the header points to the new index, the body must stay. */
  if(result != APPEND_OK && !pointer_written) fs_truncate(fd, c->base);
  return result;
}

}
