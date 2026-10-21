/**
 * @file    archive.cpp
 * @brief   Implementation of the .crls archive (see archive.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/archive.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"
#include "lib/function_ref.h"

namespace crls{

static_assert(sizeof(crypto_generichash_state) == 384 && alignof(crypto_generichash_state) == 64,
              "crypto_generichash_state must match the layout of libsodium");

namespace{

/* Shared scratch buffers: only one chunk is processed at a time, and the
   program uses one archive operation at a time. */
unsigned char plain_chunk[CHUNK];
unsigned char cipher_chunk[CHUNK + STREAM_ABYTES];
unsigned char out_chunk[CHUNK + STREAM_ABYTES];

/**
 * @brief  Computes the encrypted size of a file of the given plaintext size.
 *
 * A file occupies size / CHUNK + 1 chunks; the last chunk may be empty.
 *
 * @param  size Plaintext size in bytes.
 * @return Encrypted size in bytes.
 */
uint64_t stored_size(uint64_t size){
  return size + (size / CHUNK + 1) * STREAM_ABYTES;
}

/**
 * @brief  Derives the master key from the password with Argon2id.
 * @param  master   Output: master key of KEY_BYTES bytes.
 * @param  password Password.
 * @param  salt     Salt of SALT_BYTES bytes.
 * @param  ops      Argon2id operations limit.
 * @param  mem      Argon2id memory limit.
 * @return True on success, false if the derivation fails (e.g. out of memory).
 */
bool derive_master(unsigned char *master, const char *password, const unsigned char *salt, uint64_t ops, uint64_t mem){
  return crypto_pwhash(master, KEY_BYTES, password, strlen(password), salt,
                       ops, static_cast<size_t>(mem), crypto_pwhash_alg_argon2id13()) == 0;
}

/** @brief Why pull_entry() stopped. */
enum class pull_result{
  ok,                        /**< The whole entry was decrypted. */
  damaged,                   /**< The ciphertext does not decrypt or is truncated. */
  read_error,                /**< The source could not be read (it may have set the status). */
  sink_error                 /**< The sink failed. */
};

/**
 * @brief Receives the plaintext of an entry, one chunk at a time; returns
 *        false to stop with an error. The flag is true for the last chunk.
 */
using chunk_sink = function_ref<bool(std::span<const unsigned char> plain, bool final)>;

/**
 * @brief  Decrypts an entry chunk by chunk and passes each chunk to sink.
 * @param  src  Source the entry is read from.
 * @param  e    Entry.
 * @param  key  Key the entry was encrypted with.
 * @param  sink Receiver of the plaintext.
 * @return pull_result::ok on success, or the reason of the failure.
 */
pull_result pull_entry(byte_source &src, const archive_entry &e, const unsigned char *key, chunk_sink sink){
  crypto_secretstream_xchacha20poly1305_state state;
  pull_result result = crypto_secretstream_xchacha20poly1305_init_pull(&state, e.stream_header.data(), key) == 0
                       ? pull_result::ok : pull_result::damaged;
  bool final = false;
  uint64_t consumed = 0, produced = 0;

  while(result == pull_result::ok && !final){
    uint64_t left = e.stored - consumed;
    size_t n = left < sizeof(cipher_chunk) ? static_cast<size_t>(left) : sizeof(cipher_chunk);
    unsigned long long plain_size;
    unsigned char tag;
    if(n < STREAM_ABYTES){
      result = pull_result::damaged;
      break;
    }
    if(!src.read_at({cipher_chunk, n}, e.offset + consumed)){
      result = pull_result::read_error;
      break;
    }
    if(crypto_secretstream_xchacha20poly1305_pull(&state, plain_chunk, &plain_size, &tag,
                                                  cipher_chunk, n, NULL, 0) != 0){
      result = pull_result::damaged;
      break;
    }
    consumed += n;
    final = tag == crypto_secretstream_xchacha20poly1305_tag_final();
    if(final != (consumed == e.stored)){
      result = pull_result::damaged;
      break;
    }
    produced += plain_size;
    if(!sink({plain_chunk, static_cast<size_t>(plain_size)}, final)) result = pull_result::sink_error;
  }
  if(result == pull_result::ok && produced != e.size) result = pull_result::damaged;
  sodium_memzero(plain_chunk, sizeof(plain_chunk));
  sodium_memzero(&state, sizeof(state));
  return result;
}

/**
 * @brief Sets the status message for a failed pull_entry().
 * @param e      Entry.
 * @param result Failure.
 * @param target Path being written, for pull_result::sink_error.
 * @param remote True if the archive is remote.
 * @param path   Path of the archive, for read errors.
 */
void report_pull_error(const archive_entry &e, pull_result result, const std::string &target, bool remote,
                       const std::string &path){
  if(result == pull_result::damaged) set_status(true, "The content of %s is damaged.", e.name.c_str());
  else if(result == pull_result::sink_error) set_status(true, "Could not write %s.", target.c_str());
  /* A remote source explains its own failures (e.g. the archive changed on the server). */
  else if(!remote) set_status(true, "Could not read %s.", path.c_str());
}

}

/* ---------------- Helpers ---------------- */

void archive::derive_v3_keys(const unsigned char *master, unsigned char *pk){
  secret<32> seed;
  crypto_kdf_derive_from_key(key_.data(), KEY_BYTES, 1, KDF_CONTEXT, master);
  crypto_kdf_derive_from_key(seed.data(), seed.size(), 2, KDF_CONTEXT, master);
  crypto_sign_seed_keypair(pk, sign_sk_.data(), seed.data());
}

/**
 * @brief  Derives every key from the password, as the version requires.
 * @param  password Password.
 * @param  header   Header holding the Argon2id parameters and the salt.
 * @param  pk       Output: Ed25519 public key (version 3 only).
 * @return True on success, false if Argon2id fails.
 */
bool archive::derive_keys(const char *password, const unsigned char *header, unsigned char *pk){
  secret<KEY_BYTES> master;
  if(!derive_master(master.data(), password, header + 24, get_le(header + 8, 8), get_le(header + 16, 8))) return false;
  if(version_ >= 3) derive_v3_keys(master.data(), pk);
  else memcpy(key_.data(), master.data(), KEY_BYTES);
  return true;
}

/**
 * @brief Stores the absolute path of the archive, or path as given if it
 *        cannot be resolved.
 * @param path Path of the archive.
 */
void archive::remember_path(const std::string &path){
  if(!fs_absolute_path(path, path_)) path_ = path;
}

/**
 * @brief Sets the layout fields for the given format version.
 * @param version Format version.
 */
void archive::set_layout(int version){
  version_ = version;
  header_bytes_ = version >= 3 ? V3_HEADER_BYTES : V2_HEADER_BYTES;
  ad_len_ = version >= 3 ? V3_AD_BYTES : V2_AD_BYTES;
  pointer_pos_ = ad_len_;
}

std::optional<std::size_t> archive::find(std::string_view name) const{
  for(std::size_t i = 0; i < entries_.size(); i++)
    if(entries_[i].name == name) return i;
  return std::nullopt;
}

/* ---------------- Index ---------------- */

bool archive::build_index(const archive_entry *extra, heap_buffer &blob){
  std::size_t count = entries_.size() + (extra ? 1 : 0);
  auto entry = [&](std::size_t i) -> const archive_entry &{ return i < entries_.size() ? entries_[i] : *extra; };
  uint64_t plain_size = 4;
  for(std::size_t i = 0; i < count; i++) plain_size += 2 + entry(i).name.size() + 8 + 8 + 8 + STREAM_HEADER;
  if(plain_size > MAX_INDEX){
    set_status(true, "There are too many files in the .crls.");
    return false;
  }

  secret_buffer plain;
  if(!plain.allocate(plain_size) || !blob.allocate(NONCE_BYTES + plain_size + TAG_BYTES)){
    set_status(true, "Not enough memory to save the index.");
    return false;
  }

  unsigned char *p = plain.data();
  put_le(p, count, 4);
  p += 4;
  for(std::size_t i = 0; i < count; i++){
    const archive_entry &e = entry(i);
    put_le(p, e.name.size(), 2);
    p += 2;
    memcpy(p, e.name.data(), e.name.size());
    p += e.name.size();
    put_le(p, e.size, 8);
    put_le(p + 8, e.offset, 8);
    put_le(p + 16, e.stored, 8);
    p += 24;
    memcpy(p, e.stream_header.data(), STREAM_HEADER);
    p += STREAM_HEADER;
  }

  randombytes_buf(blob.data(), NONCE_BYTES);
  crypto_aead_xchacha20poly1305_ietf_encrypt(blob.data() + NONCE_BYTES, NULL, plain.data(), plain_size,
                                             header_ad_.data(), ad_len_, NULL, blob.data(), key_.data());
  return true;
}

/**
 * @brief  Encrypts the index, appends it to the end of fd and then updates
 *         the header to point to it.
 * @param  fd Destination file descriptor.
 * @return True on success, false otherwise.
 */
bool archive::write_index(int fd){
  heap_buffer blob;
  if(!build_index(nullptr, blob)) return false;

  uint64_t end = file_size(fd);
  unsigned char pointer[16];
  put_le(pointer, end, 8);
  put_le(pointer + 8, blob.size(), 8);

  bool ok = write_at(fd, blob.data(), blob.size(), end) && fs_sync(fd) &&
            write_at(fd, pointer, sizeof(pointer), pointer_pos_) && fs_sync(fd);
  if(!ok){
    set_status(true, "Could not write the index: %s", strerror(errno));
    fs_truncate(fd, end);
    return false;
  }
  memcpy(index_nonce_.data(), blob.data(), NONCE_BYTES);
  index_offset_ = end;
  index_length_ = blob.size();
  return true;
}

/**
 * @brief  Parses a decrypted index and validates every entry against the
 *         archive size.
 * @param  p          Decrypted index.
 * @param  size       Length of the decrypted index.
 * @param  total_size Size of the archive file.
 * @return True if the index is well formed, false otherwise.
 */
bool archive::parse_index(const unsigned char *p, uint64_t size, uint64_t total_size){
  const unsigned char *end = p + size;
  if(size < 4) return false;
  uint64_t count = get_le(p, 4);
  p += 4;

  for(uint64_t i = 0; i < count; i++){
    archive_entry e;
    if(end - p < 2) return false;
    size_t name_len = static_cast<size_t>(get_le(p, 2));
    p += 2;
    if(name_len == 0 || name_len > MAX_NAME || static_cast<size_t>(end - p) < name_len) return false;
    /* A NUL byte would hide the rest of the name from the checks made on it. */
    if(memchr(p, 0, name_len)) return false;
    e.name.assign(reinterpret_cast<const char *>(p), name_len);
    p += name_len;

    if(end - p < static_cast<std::ptrdiff_t>(24 + STREAM_HEADER)) return false;
    e.size = get_le(p, 8);
    e.offset = get_le(p + 8, 8);
    e.stored = get_le(p + 16, 8);
    p += 24;
    memcpy(e.stream_header.data(), p, STREAM_HEADER);
    p += STREAM_HEADER;

    if(e.size > total_size || e.stored != stored_size(e.size) || e.offset < header_bytes_ ||
       e.offset > total_size || e.stored > total_size - e.offset) return false;
    entries_.push_back(std::move(e));
  }
  return p == end;
}

/* ---------------- Lifecycle ---------------- */

bool archive_crypto_init(){
  return sodium_init() >= 0 && crypto_pwhash_saltbytes() == SALT_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_keybytes() == KEY_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_npubbytes() == NONCE_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_abytes() == TAG_BYTES &&
         crypto_secretstream_xchacha20poly1305_headerbytes() == STREAM_HEADER &&
         crypto_secretstream_xchacha20poly1305_abytes() == STREAM_ABYTES &&
         crypto_secretstream_xchacha20poly1305_statebytes() == sizeof(crypto_secretstream_xchacha20poly1305_state) &&
         crypto_kdf_keybytes() == KEY_BYTES && crypto_kdf_contextbytes() == strlen(KDF_CONTEXT) &&
         crypto_sign_seedbytes() == 32 && crypto_sign_publickeybytes() == SIGN_PK_BYTES &&
         crypto_sign_secretkeybytes() == SIGN_SK_BYTES && crypto_sign_bytes() == SIGN_BYTES &&
         crypto_generichash_statebytes() == sizeof(crypto_generichash_state);
}

void archive::close(){
  /* clear() keeps the memory; swapping with an empty vector releases it. */
  std::vector<archive_entry>().swap(entries_);
  key_.wipe();
  sign_sk_.wipe();
  src_.reset();
  fd_.reset();
  open_ = false;
  remote_ = false;
  path_.clear();
}

/**
 * @brief  Reads the header and the index from the source and decrypts the
 *         index.
 *
 * On success the key, the layout fields, the entries and the index location
 * are set. On failure the keys are wiped and the status explains why.
 *
 * @param  name     Name of the archive for error messages.
 * @param  password Password used to derive the key, or nullptr to keep the
 *                  keys already derived (reloading an archive that changed).
 * @return True on success, false otherwise.
 */
bool archive::load(const std::string &name, const char *password){
  unsigned char header[MAX_HEADER_BYTES];
  uint64_t total = src_->size();
  if(total < V2_HEADER_BYTES || !src_->read_at({header, V2_HEADER_BYTES}, 0) ||
     memcmp(header, MAGIC, MAGIC_BYTES) != 0){
    set_status(true, "%s is not a valid .crls file.", name.c_str());
    return false;
  }
  if((header[6] != 2 && header[6] != 3) || header[7] != 1){
    set_status(true, "Unsupported .crls version (%d).", header[6]);
    return false;
  }
  set_layout(header[6]);
  if(total < header_bytes_ || !src_->read_at({header + V2_HEADER_BYTES, header_bytes_ - V2_HEADER_BYTES}, V2_HEADER_BYTES)){
    set_status(true, "%s is not a valid .crls file.", name.c_str());
    return false;
  }

  uint64_t ops = get_le(header + 8, 8), mem = get_le(header + 16, 8);
  uint64_t offset = get_le(header + pointer_pos_, 8), length = get_le(header + pointer_pos_ + 8, 8);
  if(ops < 1 || ops > MAX_OPSLIMIT || mem < 8192 || mem > MAX_MEMLIMIT ||
     offset < header_bytes_ || offset > total || length < NONCE_BYTES + TAG_BYTES + 4 ||
     length > NONCE_BYTES + MAX_INDEX + TAG_BYTES || length > total - offset){
    set_status(true, "The .crls header is damaged.");
    return false;
  }

  /* The index length comes from the file: allocations of that size are checked. */
  heap_buffer blob;
  secret_buffer plain;
  if(!blob.allocate(length) || !plain.allocate(length) || !src_->read_at(blob.span(), offset)){
    set_status(true, "Could not read the .crls index.");
    return false;
  }

  /* A wrong password, or a tampered public key, makes the index fail to decrypt. */
  unsigned char pk[SIGN_PK_BYTES];
  if(password && !derive_keys(password, header, pk)){
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  unsigned long long plain_size;
  bool ok = crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plain_size, NULL,
                                                       blob.data() + NONCE_BYTES, length - NONCE_BYTES,
                                                       header, ad_len_, blob.data(), key_.data()) == 0;
  entries_.clear();
  if(ok && !parse_index(plain.data(), plain_size, total)){
    entries_.clear();
    ok = false;
    set_status(true, "The decrypted index is corrupt.");
  }else if(!ok){
    set_status(true, "Wrong password or damaged file.");
  }

  if(!ok){
    if(password){
      key_.wipe();
      sign_sk_.wipe();
    }
    return false;
  }

  memcpy(index_nonce_.data(), blob.data(), NONCE_BYTES);
  memcpy(header_ad_.data(), header, ad_len_);
  index_offset_ = offset;
  index_length_ = length;
  return true;
}

bool archive::open(const std::string &path, const char *password){
  close();
  unique_fd fd(::open(path.c_str(), O_RDWR | O_BINARY));
  if(!fd){
    set_status(true, "Could not open %s: %s", path.c_str(), strerror(errno));
    return false;
  }
  if(!fs_lock(fd.get())){
    set_status(true, "%s is already open in another program.", path.c_str());
    return false;
  }

  src_ = std::make_unique<fd_source>(fd.get());
  if(!load(path, password)){
    src_.reset();
    return false;
  }

  fd_ = std::move(fd);
  open_ = true;
  remember_path(path);
  return true;
}

bool archive::open_source(std::unique_ptr<byte_source> src, const std::string &name, const char *password){
  close();
  src_ = std::move(src);
  if(!load(name, password)){
    src_.reset();
    return false;
  }
  open_ = true;
  remote_ = true;
  path_ = name;
  return true;
}

bool archive::reload(){
  return load(path_, nullptr);
}

bool archive::create(const std::string &path, const char *password){
  return create(path, password, crypto_pwhash_opslimit_moderate(), crypto_pwhash_memlimit_moderate());
}

bool archive::create(const std::string &path, const char *password, uint64_t ops, uint64_t mem){
  close();
  status_error = false;
  unique_fd fd(::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600));
  if(!fd){
    set_status(true, "Could not create %s: %s", path.c_str(), strerror(errno));
    return false;
  }
  fs_lock(fd.get());

  set_layout(3);
  unsigned char header[MAX_HEADER_BYTES] = {0};
  memcpy(header, MAGIC, MAGIC_BYTES);
  header[6] = 3;
  header[7] = 1;
  put_le(header + 8, ops, 8);
  put_le(header + 16, mem, 8);
  randombytes_buf(header + 24, SALT_BYTES);

  if(!derive_keys(password, header, header + 40)){
    fd.reset();
    unlink(path.c_str());
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  memcpy(header_ad_.data(), header, ad_len_);
  if(!write_at(fd.get(), header, header_bytes_, 0) || !write_index(fd.get())){
    if(!status_error) set_status(true, "Could not write %s: %s", path.c_str(), strerror(errno));
    key_.wipe();
    sign_sk_.wipe();
    fd.reset();
    unlink(path.c_str());
    return false;
  }

  fd_ = std::move(fd);
  src_ = std::make_unique<fd_source>(fd_.get());
  open_ = true;
  remember_path(path);
  return true;
}

/* ---------------- Queries ---------------- */

uint64_t archive::total_size() const{
  uint64_t total = 0;
  for(const archive_entry &e : entries_) total += e.size;
  return total;
}

uint64_t archive::wasted_space() const{
  uint64_t used = header_bytes_ + index_length_;
  for(const archive_entry &e : entries_) used += e.stored;
  uint64_t total = src_ ? src_->size() : 0;
  return total > used ? total - used : 0;
}

bool has_crls_ext(std::string_view name){
  return name.size() > 5 && name.ends_with(".crls");
}

/* ---------------- Modifications ---------------- */

bool archive::encrypt_file(const std::string &path, int fd, uint64_t position, archive_entry &e){
  const char *name = fs_basename(path.c_str());
  if(strlen(name) == 0 || strlen(name) > MAX_NAME){
    set_status(true, "Invalid file name.");
    return false;
  }
  if(find(name)){
    set_status(true, "There is already a file named %s in the .crls.", name);
    return false;
  }

  FILE *source = fopen(path.c_str(), "rb");
  struct stat info;
  if(!source || fstat(fileno(source), &info) != 0 || !S_ISREG(info.st_mode)){
    if(source) fclose(source);
    set_status(true, "Could not read %s.", path.c_str());
    return false;
  }
  if(fd_ && fs_same_file(fd_.get(), fileno(source))){
    fclose(source);
    set_status(true, "A .crls cannot be added to itself.");
    return false;
  }

  e = archive_entry();
  e.name = name;
  e.offset = position;

  crypto_secretstream_xchacha20poly1305_state state;
  crypto_secretstream_xchacha20poly1305_init_push(&state, e.stream_header.data(), key_.data());

  bool ok = true, final = false;
  while(ok && !final){
    size_t n = fread(plain_chunk, 1, CHUNK, source);
    if(ferror(source)){
      ok = false;
      break;
    }
    final = n < CHUNK;
    unsigned char tag = final ? crypto_secretstream_xchacha20poly1305_tag_final()
                              : crypto_secretstream_xchacha20poly1305_tag_message();
    unsigned long long cipher_size;
    crypto_secretstream_xchacha20poly1305_push(&state, cipher_chunk, &cipher_size, plain_chunk, n, NULL, 0, tag);
    ok = write_at(fd, cipher_chunk, static_cast<size_t>(cipher_size), position);
    position += cipher_size;
    e.size += n;
  }
  fclose(source);
  sodium_memzero(plain_chunk, sizeof(plain_chunk));
  sodium_memzero(&state, sizeof(state));
  e.stored = position - e.offset;

  if(!ok){
    set_status(true, "Could not add %s: %s", name, strerror(errno));
    return false;
  }
  return true;
}

bool archive::add_file(const std::string &path){
  archive_entry e;
  uint64_t end = file_size(fd_.get());
  if(!encrypt_file(path, fd_.get(), end, e)){
    fs_truncate(fd_.get(), end);
    return false;
  }

  entries_.push_back(e);
  if(!write_index(fd_.get())){
    entries_.pop_back();
    fs_truncate(fd_.get(), e.offset);
    return false;
  }

  char size_text[32];
  format_size(e.size, size_text, sizeof(size_text));
  set_status(false, "Added %s (%s).", e.name.c_str(), size_text);
  return true;
}

void archive::record_append(const archive_entry &e, uint64_t index_offset, uint64_t index_length,
                            std::span<const unsigned char, NONCE_BYTES> nonce){
  entries_.push_back(e);
  index_offset_ = index_offset;
  index_length_ = index_length;
  memcpy(index_nonce_.data(), nonce.data(), NONCE_BYTES);
}

bool archive::remove(std::size_t index){
  archive_entry removed = entries_[index];
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));

  if(!write_index(fd_.get())){
    /* Restore the entry at its original position. */
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(index), removed);
    return false;
  }

  set_status(false, "Deleted %s. Use Compact to reclaim the space.", removed.name.c_str());
  return true;
}

/* ---------------- Compaction ---------------- */

bool archive::compact(){
  status_error = false;
  std::string tmp_path = path_ + ".tmp";
  unique_fd fd(::open(tmp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600));
  if(!fd){
    set_status(true, "Could not create %s: %s", tmp_path.c_str(), strerror(errno));
    return false;
  }

  /* Everything that compaction changes, to restore it on failure. */
  std::vector<archive_entry> saved = entries_;
  secret<KEY_BYTES> saved_key;
  secret<SIGN_SK_BYTES> saved_sk;
  std::array<unsigned char, MAX_AD_BYTES> saved_ad = header_ad_;
  int saved_version = version_;
  uint64_t saved_index_offset = index_offset_, saved_index_length = index_length_;
  memcpy(saved_key.data(), key_.data(), KEY_BYTES);
  memcpy(saved_sk.data(), sign_sk_.data(), SIGN_SK_BYTES);

  /* A version 2 archive is converted: its key is the master key of version 3. */
  bool upgrade = version_ < 3;
  unsigned char header[MAX_HEADER_BYTES] = {0};
  memcpy(header, header_ad_.data(), ad_len_);
  if(upgrade){
    header[6] = 3;
    set_layout(3);
    derive_v3_keys(saved_key.data(), header + 40);
    memcpy(header_ad_.data(), header, ad_len_);
  }
  bool ok = write_at(fd.get(), header, header_bytes_, 0);

  uint64_t position = header_bytes_;
  for(std::size_t i = 0; ok && i < entries_.size(); i++){
    archive_entry &e = entries_[i];
    if(upgrade){
      /* Decrypt with the old key and encrypt again with the new one. */
      crypto_secretstream_xchacha20poly1305_state push_state;
      std::array<unsigned char, STREAM_HEADER> stream_header;
      crypto_secretstream_xchacha20poly1305_init_push(&push_state, stream_header.data(), key_.data());
      uint64_t written = position;
      pull_result result = pull_entry(*src_, e, saved_key.data(), [&](std::span<const unsigned char> plain, bool final){
        unsigned char tag = final ? crypto_secretstream_xchacha20poly1305_tag_final()
                                  : crypto_secretstream_xchacha20poly1305_tag_message();
        unsigned long long cipher_size;
        crypto_secretstream_xchacha20poly1305_push(&push_state, out_chunk, &cipher_size, plain.data(), plain.size(),
                                                   NULL, 0, tag);
        bool sent = write_at(fd.get(), out_chunk, static_cast<size_t>(cipher_size), written);
        written += cipher_size;
        return sent;
      });
      sodium_memzero(&push_state, sizeof(push_state));
      ok = result == pull_result::ok && written - position == e.stored;
      if(ok) e.stream_header = stream_header;
      else if(result != pull_result::ok) report_pull_error(e, result, tmp_path, remote_, path_);
    }else{
      for(uint64_t copied = 0; ok && copied < e.stored; ){
        size_t len = e.stored - copied < sizeof(cipher_chunk) ? static_cast<size_t>(e.stored - copied) : sizeof(cipher_chunk);
        ok = src_->read_at({cipher_chunk, len}, e.offset + copied) &&
             write_at(fd.get(), cipher_chunk, len, position + copied);
        copied += len;
      }
    }
    e.offset = position;
    position += e.stored;
  }

  ok = ok && write_index(fd.get()) && fs_replace_file(tmp_path, fd, path_, fd_);

  if(!ok){
    entries_ = saved;
    memcpy(key_.data(), saved_key.data(), KEY_BYTES);
    memcpy(sign_sk_.data(), saved_sk.data(), SIGN_SK_BYTES);
    header_ad_ = saved_ad;
    set_layout(saved_version);
    index_offset_ = saved_index_offset;
    index_length_ = saved_index_length;

    fd.reset();
    unlink(tmp_path.c_str());
    if(!fd_){
      close();
      set_status(true, "Could not reopen the archive after compacting.");
      return false;
    }
    /* On Windows the descriptor may have been reopened. */
    src_ = std::make_unique<fd_source>(fd_.get());
    if(!status_error) set_status(true, "Could not compact: %s", strerror(errno));
    return false;
  }

  src_ = std::make_unique<fd_source>(fd_.get());
  return true;
}

/* ---------------- Extraction ---------------- */

extract_result archive::extract(std::size_t index, const std::string &dir){
  const archive_entry &e = entries_[index];
  if(!fs_is_plain_name(e.name.c_str())){
    set_status(true, "Name not allowed in the .crls: %s", e.name.c_str());
    return extract_result::failed;
  }

  char path[PATH_MAX];
  join_path(path, sizeof(path), dir.c_str(), e.name.c_str());
  unique_fd out(::open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600));
  if(!out){
    if(errno == EEXIST) return extract_result::skipped;
    set_status(true, "Could not create %s: %s", path, strerror(errno));
    return extract_result::failed;
  }

  uint64_t written = 0;
  pull_result result = pull_entry(*src_, e, key_.data(), [&](std::span<const unsigned char> plain, bool){
    bool ok = write_at(out.get(), plain.data(), plain.size(), written);
    written += plain.size();
    return ok;
  });
  if(::close(out.release()) != 0 && result == pull_result::ok) result = pull_result::sink_error;
  if(result != pull_result::ok){
    unlink(path);
    report_pull_error(e, result, path, remote_, path_);
    return extract_result::failed;
  }
  return extract_result::extracted;
}

}
