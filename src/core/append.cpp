/**
 * @file    append.cpp
 * @brief   Implementation of signed appends (see append.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/append.h"

#include <algorithm>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

namespace crls{

namespace{

/* Scratch buffer used to stream bodies, one at a time. Static for the same
   reason as the chunks of archive.cpp: too large for the stack on Windows. */
std::array<unsigned char, CHUNK> body_chunk;

/**
 * @brief  Computes the BLAKE2b-256 hash of the first len bytes of a file.
 * @param  fd   File descriptor.
 * @param  len  Number of bytes.
 * @param  hash Output.
 * @return True on success, false on a read error.
 */
bool hash_file(int fd, uint64_t len, std::span<unsigned char, BODY_HASH_BYTES> hash){
  crypto_generichash_state state;
  crypto_generichash_init(&state, nullptr, 0, BODY_HASH_BYTES);
  for(uint64_t done = 0; done < len; ){
    std::span<unsigned char> chunk =
        std::span(body_chunk).first(static_cast<size_t>(std::min<uint64_t>(len - done, body_chunk.size())));
    if(!read_at(fd, chunk, done)) return false;
    crypto_generichash_update(&state, chunk.data(), chunk.size());
    done += chunk.size();
  }
  crypto_generichash_final(&state, hash.data(), hash.size());
  return true;
}

}

std::array<unsigned char, APPEND_MESSAGE_BYTES> append_message(std::span<const unsigned char, SALT_BYTES> salt,
                                                               uint64_t index_offset, uint64_t index_length,
                                                               const append_claim &c){
  static_assert(APPEND_DOMAIN_BYTES + SALT_BYTES + 16 + NONCE_BYTES + 16 + BODY_HASH_BYTES == APPEND_MESSAGE_BYTES,
                "APPEND_MESSAGE_BYTES must match the fields of the message");
  std::array<unsigned char, APPEND_MESSAGE_BYTES> msg{};
  byte_writer out(msg);
  out.bytes(std::string_view(APPEND_DOMAIN, APPEND_DOMAIN_BYTES));
  out.bytes(salt);
  out.le<8>(index_offset);
  out.le<8>(index_length);
  out.bytes(c.index_nonce);
  out.le<8>(c.content_length);
  out.le<8>(c.index_length);
  out.bytes(c.body_hash);
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
  std::optional<archive_entry> e = a.encrypt_file(path, body_.get(), 0);
  if(e) entry_ = std::move(*e);
  if(!e || !rebase(a)){
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
  std::ranges::copy(a.index_nonce(), claim.index_nonce.begin());
  entry_.offset = claim.base;

  std::optional<heap_buffer> blob = a.build_index({&entry_, 1});
  if(!blob) return false;
  bool ok = write_at(body_.get(), blob->span(), entry_.stored) &&
            fs_truncate(body_.get(), entry_.stored + blob->size());
  std::ranges::copy(blob->span().first(NONCE_BYTES), new_nonce_.begin());

  claim.content_length = entry_.stored + blob->size();
  claim.index_length = blob->size();
  ok = ok && hash_file(body_.get(), claim.content_length, claim.body_hash);
  if(!ok){
    set_status(true, "Could not write the temporary file.");
    return false;
  }

  std::array<unsigned char, APPEND_MESSAGE_BYTES> msg = append_message(a.salt(), a.index_offset(), a.index_length(), claim);
  crypto_sign_detached(claim.signature.data(), nullptr, msg.data(), msg.size(), a.signing_key().data());
  return true;
}

void append_upload::applied(archive &a) const{
  a.record_append(entry_, claim.base + entry_.stored, claim.index_length, new_nonce_);
}

/* ---------------- Server ---------------- */

append_result append_load_state(int fd, append_state &st){
  std::array<unsigned char, V3_HEADER_BYTES> header;
  std::span<const unsigned char, V3_HEADER_BYTES> head(header);
  uint64_t total = file_size(fd);
  if(total < V3_HEADER_BYTES || !read_at(fd, header, 0) || !std::ranges::equal(head.first<MAGIC.size()>(), MAGIC))
    return append_result::bad_request;
  /* In version 2, bytes 40 to 56 are the index pointer, not a key. */
  if(header[HEADER_VERSION_POS] != 3 || header[HEADER_ALGORITHM_POS] != 1) return append_result::unsupported;

  std::ranges::copy(head.subspan<HEADER_SALT_POS, SALT_BYTES>(), st.salt.begin());
  std::ranges::copy(head.subspan<HEADER_PK_POS, SIGN_PK_BYTES>(), st.public_key.begin());
  st.index_offset = get_le(head.subspan<V3_AD_BYTES, 8>());
  st.index_length = get_le(head.subspan<V3_AD_BYTES + 8, 8>());
  if(st.index_offset < V3_HEADER_BYTES || st.index_offset > total ||
     st.index_length < NONCE_BYTES + TAG_BYTES + 4 || st.index_length > total - st.index_offset)
    return append_result::bad_request;
  if(!read_at(fd, st.index_nonce, st.index_offset)) return append_result::io;

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
  crypto_generichash_init(&state, nullptr, 0, BODY_HASH_BYTES);
  append_result result = append_result::ok;

  for(uint64_t done = 0; result == append_result::ok && done < c.content_length; ){
    std::span<unsigned char> chunk =
        std::span(body_chunk).first(static_cast<size_t>(std::min<uint64_t>(c.content_length - done, body_chunk.size())));
    if(!read(chunk)) result = append_result::bad_request;
    else if(!write_at(fd, chunk, c.base + done)) result = append_result::io;
    else crypto_generichash_update(&state, chunk.data(), chunk.size());
    done += chunk.size();
  }

  std::array<unsigned char, BODY_HASH_BYTES> hash;
  crypto_generichash_final(&state, hash.data(), hash.size());
  if(result == append_result::ok && sodium_memcmp(hash.data(), c.body_hash.data(), hash.size()) != 0)
    result = append_result::bad_request;

  bool pointer_written = false;
  if(result == append_result::ok){
    std::array<unsigned char, 16> pointer;
    byte_writer out(pointer);
    out.le<8>(c.base + c.content_length - c.index_length);
    out.le<8>(c.index_length);
    /* The body must be on disk before the header points to it. */
    if(!fs_sync(fd)) result = append_result::io;
    else{
      pointer_written = write_at(fd, pointer, V3_AD_BYTES);
      if(!pointer_written || !fs_sync(fd)) result = append_result::io;
    }
  }
  /* Once the header points to the new index, the body must stay. */
  if(result != append_result::ok && !pointer_written) fs_truncate(fd, c.base);
  return result;
}

}
