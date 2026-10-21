/**
 * @file    append.cpp
 * @brief   Implementation of signed appends (see append.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/append.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

namespace crls{

namespace{

/* Scratch buffer used to stream bodies, one at a time. */
unsigned char body_chunk[CHUNK];

/**
 * @brief  Computes the BLAKE2b-256 hash of the first len bytes of a file.
 * @param  fd   File descriptor.
 * @param  len  Number of bytes.
 * @param  hash Output.
 * @return True on success, false on a read error.
 */
bool hash_file(int fd, uint64_t len, std::span<unsigned char, BODY_HASH_BYTES> hash){
  crypto_generichash_state state;
  crypto_generichash_init(&state, NULL, 0, BODY_HASH_BYTES);
  for(uint64_t done = 0; done < len; ){
    size_t n = len - done < sizeof(body_chunk) ? static_cast<size_t>(len - done) : sizeof(body_chunk);
    if(!read_at(fd, body_chunk, n, done)) return false;
    crypto_generichash_update(&state, body_chunk, n);
    done += n;
  }
  crypto_generichash_final(&state, hash.data(), BODY_HASH_BYTES);
  return true;
}

}

std::array<unsigned char, APPEND_MESSAGE_BYTES> append_message(std::span<const unsigned char, SALT_BYTES> salt,
                                                               uint64_t index_offset, uint64_t index_length,
                                                               const append_claim &c){
  static_assert(APPEND_DOMAIN_BYTES + SALT_BYTES + 16 + NONCE_BYTES + 16 + BODY_HASH_BYTES == APPEND_MESSAGE_BYTES,
                "APPEND_MESSAGE_BYTES must match the fields of the message");
  std::array<unsigned char, APPEND_MESSAGE_BYTES> msg{};
  unsigned char *p = msg.data();
  memcpy(p, APPEND_DOMAIN, APPEND_DOMAIN_BYTES);
  p += APPEND_DOMAIN_BYTES;
  memcpy(p, salt.data(), SALT_BYTES);
  p += SALT_BYTES;
  put_le(p, index_offset, 8);
  put_le(p + 8, index_length, 8);
  p += 16;
  memcpy(p, c.index_nonce.data(), NONCE_BYTES);
  p += NONCE_BYTES;
  put_le(p, c.content_length, 8);
  put_le(p + 8, c.index_length, 8);
  p += 16;
  memcpy(p, c.body_hash.data(), BODY_HASH_BYTES);
  return msg;
}

/* ---------------- Client ---------------- */

bool append_upload::prepare(archive &a, const std::string &path){
  claim = append_claim();
  body_.reset();
  if(a.version() < 3){
    set_status(true, "Uploads need a version 3 archive. Compact it first.");
    return false;
  }
  body_ = fs_temp_file();
  if(!body_){
    set_status(true, "Could not create a temporary file.");
    return false;
  }
  /* The ciphertext does not depend on where it is stored, so it is written at
     offset 0 of the body and only the index records its final offset. */
  if(!a.encrypt_file(path, body_.get(), 0, entry_) || !rebase(a)){
    body_.reset();
    return false;
  }
  return true;
}

bool append_upload::rebase(archive &a){
  if(a.find(entry_.name)){
    set_status(true, "There is already a file named %s in the .crls.", entry_.name.c_str());
    return false;
  }
  claim.base = a.index_offset() + a.index_length();
  memcpy(claim.index_nonce.data(), a.index_nonce().data(), NONCE_BYTES);
  entry_.offset = claim.base;

  heap_buffer blob;
  if(!a.build_index(&entry_, blob)) return false;
  bool ok = write_at(body_.get(), blob.data(), blob.size(), entry_.stored) &&
            fs_truncate(body_.get(), entry_.stored + blob.size());
  memcpy(new_nonce_.data(), blob.data(), NONCE_BYTES);

  claim.content_length = entry_.stored + blob.size();
  claim.index_length = blob.size();
  ok = ok && hash_file(body_.get(), claim.content_length, claim.body_hash);
  if(!ok){
    set_status(true, "Could not write the temporary file.");
    return false;
  }

  std::span<const unsigned char, SALT_BYTES> salt(a.header_ad().data() + 24, SALT_BYTES);
  std::array<unsigned char, APPEND_MESSAGE_BYTES> msg = append_message(salt, a.index_offset(), a.index_length(), claim);
  crypto_sign_detached(claim.signature.data(), NULL, msg.data(), msg.size(), a.signing_key().data());
  return true;
}

void append_upload::applied(archive &a) const{
  a.record_append(entry_, claim.base + entry_.stored, claim.index_length, new_nonce_);
}

/* ---------------- Server ---------------- */

append_result append_load_state(int fd, append_state &st){
  unsigned char header[V3_HEADER_BYTES];
  uint64_t total = file_size(fd);
  if(total < V3_HEADER_BYTES || !read_at(fd, header, V3_HEADER_BYTES, 0) ||
     memcmp(header, MAGIC, MAGIC_BYTES) != 0) return append_result::bad_request;
  /* In version 2, bytes 40 to 56 are the index pointer, not a key. */
  if(header[6] != 3 || header[7] != 1) return append_result::unsupported;

  memcpy(st.salt.data(), header + 24, SALT_BYTES);
  memcpy(st.public_key.data(), header + 40, SIGN_PK_BYTES);
  st.index_offset = get_le(header + V3_AD_BYTES, 8);
  st.index_length = get_le(header + V3_AD_BYTES + 8, 8);
  if(st.index_offset < V3_HEADER_BYTES || st.index_offset > total ||
     st.index_length < NONCE_BYTES + TAG_BYTES + 4 || st.index_length > total - st.index_offset)
    return append_result::bad_request;
  if(!read_at(fd, st.index_nonce.data(), NONCE_BYTES, st.index_offset)) return append_result::io;

  /* Every committed write ends with the current index; anything after it is
     the remainder of an interrupted upload. */
  uint64_t end = st.index_offset + st.index_length;
  if(total > end && !fs_truncate(fd, end)) return append_result::io;
  return append_result::ok;
}

append_result append_check(const append_state &st, const append_claim &c, uint64_t max_body){
  if(c.base != st.index_offset + st.index_length ||
     sodium_memcmp(c.index_nonce.data(), st.index_nonce.data(), NONCE_BYTES) != 0) return append_result::conflict;
  if(c.content_length > max_body) return append_result::too_large;
  if(c.index_length < NONCE_BYTES + TAG_BYTES + 4 || c.index_length > NONCE_BYTES + MAX_INDEX + TAG_BYTES ||
     c.index_length > c.content_length) return append_result::bad_request;

  std::array<unsigned char, APPEND_MESSAGE_BYTES> msg = append_message(st.salt, st.index_offset, st.index_length, c);
  if(crypto_sign_verify_detached(c.signature.data(), msg.data(), msg.size(), st.public_key.data()) != 0)
    return append_result::forbidden;
  return append_result::ok;
}

append_result append_commit(int fd, const append_claim &c, append_reader read){
  crypto_generichash_state state;
  crypto_generichash_init(&state, NULL, 0, BODY_HASH_BYTES);
  append_result result = append_result::ok;

  for(uint64_t done = 0; result == append_result::ok && done < c.content_length; ){
    size_t n = c.content_length - done < sizeof(body_chunk) ? static_cast<size_t>(c.content_length - done)
                                                              : sizeof(body_chunk);
    if(!read({body_chunk, n})) result = append_result::bad_request;
    else if(!write_at(fd, body_chunk, n, c.base + done)) result = append_result::io;
    else crypto_generichash_update(&state, body_chunk, n);
    done += n;
  }

  unsigned char hash[BODY_HASH_BYTES];
  crypto_generichash_final(&state, hash, BODY_HASH_BYTES);
  if(result == append_result::ok && sodium_memcmp(hash, c.body_hash.data(), BODY_HASH_BYTES) != 0)
    result = append_result::bad_request;

  bool pointer_written = false;
  if(result == append_result::ok){
    unsigned char pointer[16];
    put_le(pointer, c.base + c.content_length - c.index_length, 8);
    put_le(pointer + 8, c.index_length, 8);
    /* The body must be on disk before the header points to it. */
    if(!fs_sync(fd) || !(pointer_written = write_at(fd, pointer, sizeof(pointer), V3_AD_BYTES)) || !fs_sync(fd))
      result = append_result::io;
  }
  /* Once the header points to the new index, the body must stay. */
  if(result != append_result::ok && !pointer_written) fs_truncate(fd, c.base);
  return result;
}

}
