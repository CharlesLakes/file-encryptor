/**
 * @file    archive.cpp
 * @brief   Implementation of the .crls archive (see archive.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/archive.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <numeric>
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
   program uses one archive operation at a time. They are static because a
   64 KB array is too large for the stack on Windows (1 MB in total) and
   allocating one per chunk would be wasted work. The plaintext one wipes
   itself on wipe() and at exit. */
secret<CHUNK> plain_chunk;
std::array<unsigned char, CHUNK + STREAM_ABYTES> cipher_chunk;
std::array<unsigned char, CHUNK + STREAM_ABYTES> out_chunk;

/** @brief Deleter of a FILE. */
struct file_closer{
  void operator()(FILE *f) const noexcept{ fclose(f); }
};

/** @brief A FILE that is closed when it goes out of scope. */
using unique_file = std::unique_ptr<FILE, file_closer>;

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
 * @param  master   Output: master key.
 * @param  password Password.
 * @param  salt     Salt.
 * @param  ops      Argon2id operations limit.
 * @param  mem      Argon2id memory limit.
 * @return True on success, false if the derivation fails (e.g. out of memory).
 */
bool derive_master(std::span<unsigned char, KEY_BYTES> master, const char *password,
                   std::span<const unsigned char, SALT_BYTES> salt, uint64_t ops, uint64_t mem){
  return crypto_pwhash(master.data(), master.size(), password, strlen(password), salt.data(),
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
    size_t n = static_cast<size_t>(std::min<uint64_t>(e.stored - consumed, cipher_chunk.size()));
    unsigned long long plain_size;
    unsigned char tag;
    if(n < STREAM_ABYTES){
      result = pull_result::damaged;
      break;
    }
    if(!src.read_at(std::span(cipher_chunk).first(n), e.offset + consumed)){
      result = pull_result::read_error;
      break;
    }
    if(crypto_secretstream_xchacha20poly1305_pull(&state, plain_chunk.data(), &plain_size, &tag,
                                                  cipher_chunk.data(), n, nullptr, 0) != 0){
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
    if(!sink(std::span<const unsigned char>(plain_chunk.span()).first(static_cast<size_t>(plain_size)), final))
      result = pull_result::sink_error;
  }
  if(result == pull_result::ok && produced != e.size) result = pull_result::damaged;
  plain_chunk.wipe();
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

void archive::derive_v3_keys(std::span<const unsigned char, KEY_BYTES> master, std::span<unsigned char, SIGN_PK_BYTES> pk){
  secret<32> seed;
  crypto_kdf_derive_from_key(key_.data(), key_.size(), 1, KDF_CONTEXT.data(), master.data());
  crypto_kdf_derive_from_key(seed.data(), seed.size(), 2, KDF_CONTEXT.data(), master.data());
  crypto_sign_seed_keypair(pk.data(), sign_sk_.data(), seed.data());
}

/**
 * @brief  Derives every key from the password, as the version requires.
 * @param  password Password.
 * @param  header   Header holding the Argon2id parameters and the salt.
 * @param  pk       Output: Ed25519 public key (version 3 only).
 * @return True on success, false if Argon2id fails.
 */
bool archive::derive_keys(const char *password, std::span<const unsigned char, MAX_HEADER_BYTES> header,
                          std::span<unsigned char, SIGN_PK_BYTES> pk){
  secret<KEY_BYTES> master;
  if(!derive_master(master.span(), password, header.subspan<HEADER_SALT_POS, SALT_BYTES>(),
                    get_le(header.subspan<HEADER_OPS_POS, 8>()), get_le(header.subspan<HEADER_MEM_POS, 8>())))
    return false;
  if(version_ >= 3) derive_v3_keys(master.span(), pk);
  else std::ranges::copy(master.span(), key_.span().begin());
  return true;
}

/**
 * @brief Stores the absolute path of the archive, or path as given if it
 *        cannot be resolved.
 * @param path Path of the archive.
 */
void archive::remember_path(const std::string &path){
  path_ = fs_absolute_path(path).value_or(path);
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
  auto it = std::ranges::find(entries_, name, &archive_entry::name);
  if(it == entries_.end()) return std::nullopt;
  return static_cast<std::size_t>(it - entries_.begin());
}

/* ---------------- Index ---------------- */

std::optional<heap_buffer> archive::build_index(std::span<const archive_entry> extra){
  auto record_size = [](const archive_entry &e){ return uint64_t{2} + e.name.size() + 8 + 8 + 8 + STREAM_HEADER; };
  uint64_t plain_size = 4;
  for(const archive_entry &e : entries_) plain_size += record_size(e);
  for(const archive_entry &e : extra) plain_size += record_size(e);
  if(plain_size > MAX_INDEX){
    set_status(true, "There are too many files in the .crls.");
    return std::nullopt;
  }

  secret_buffer plain;
  heap_buffer blob;
  if(!plain.allocate(plain_size) || !blob.allocate(NONCE_BYTES + plain_size + TAG_BYTES)){
    set_status(true, "Not enough memory to save the index.");
    return std::nullopt;
  }

  byte_writer out(plain.span());
  auto write_record = [&](const archive_entry &e){
    out.le<2>(e.name.size());
    out.bytes(e.name);
    out.le<8>(e.size);
    out.le<8>(e.offset);
    out.le<8>(e.stored);
    out.bytes(e.stream_header);
  };
  out.le<4>(entries_.size() + extra.size());
  for(const archive_entry &e : entries_) write_record(e);
  for(const archive_entry &e : extra) write_record(e);

  std::span<unsigned char> nonce = blob.span().first(NONCE_BYTES);
  randombytes_buf(nonce.data(), nonce.size());
  crypto_aead_xchacha20poly1305_ietf_encrypt(blob.data() + NONCE_BYTES, nullptr, plain.data(), plain_size,
                                             header_ad_.data(), ad_len_, nullptr, nonce.data(), key_.data());
  return blob;
}

/**
 * @brief  Encrypts the index, appends it to the end of fd and then updates
 *         the header to point to it.
 * @param  fd Destination file descriptor.
 * @return True on success, false otherwise.
 */
bool archive::write_index(int fd){
  std::optional<heap_buffer> blob = build_index();
  if(!blob) return false;

  uint64_t end = file_size(fd);
  std::array<unsigned char, 16> pointer;
  byte_writer out(pointer);
  out.le<8>(end);
  out.le<8>(blob->size());

  bool ok = write_at(fd, blob->span(), end) && fs_sync(fd) &&
            write_at(fd, pointer, pointer_pos_) && fs_sync(fd);
  if(!ok){
    set_status(true, "Could not write the index: %s", strerror(errno));
    fs_truncate(fd, end);
    return false;
  }
  std::ranges::copy(blob->span().first(NONCE_BYTES), index_nonce_.begin());
  index_offset_ = end;
  index_length_ = blob->size();
  return true;
}

/**
 * @brief  Parses a decrypted index and validates every entry against the
 *         archive size.
 * @param  plain      Decrypted index.
 * @param  total_size Size of the archive file.
 * @return True if the index is well formed, false otherwise.
 */
bool archive::parse_index(std::span<const unsigned char> plain, uint64_t total_size){
  byte_reader in(plain);
  uint64_t count = in.le<4>();
  if(!in) return false;

  for(uint64_t i = 0; i < count; i++){
    archive_entry e;
    size_t name_len = static_cast<size_t>(in.le<2>());
    if(!in || name_len == 0 || name_len > MAX_NAME) return false;
    std::span<const unsigned char> name = in.bytes(name_len);
    /* A NUL byte would hide the rest of the name from the checks made on it. */
    if(!in || std::ranges::find(name, 0) != name.end()) return false;
    e.name.assign(name.begin(), name.end());

    e.size = in.le<8>();
    e.offset = in.le<8>();
    e.stored = in.le<8>();
    in.copy_to(e.stream_header);
    if(!in) return false;

    if(e.size > total_size || e.stored != stored_size(e.size) || e.offset < header_bytes_ ||
       e.offset > total_size || e.stored > total_size - e.offset) return false;
    entries_.push_back(std::move(e));
  }
  return in.remaining() == 0;
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
         crypto_kdf_keybytes() == KEY_BYTES && crypto_kdf_contextbytes() == KDF_CONTEXT.size() &&
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
  std::array<unsigned char, MAX_HEADER_BYTES> header{};
  std::span<unsigned char> head(header);
  uint64_t total = src_->size();
  if(total < V2_HEADER_BYTES || !src_->read_at(head.first(V2_HEADER_BYTES), 0) ||
     !std::ranges::equal(head.first(MAGIC.size()), MAGIC)){
    set_status(true, "%s is not a valid .crls file.", name.c_str());
    return false;
  }
  int version = header[HEADER_VERSION_POS];
  if((version != 2 && version != 3) || header[HEADER_ALGORITHM_POS] != 1){
    set_status(true, "Unsupported .crls version (%d).", version);
    return false;
  }
  set_layout(version);
  if(total < header_bytes_ ||
     !src_->read_at(head.subspan(V2_HEADER_BYTES, header_bytes_ - V2_HEADER_BYTES), V2_HEADER_BYTES)){
    set_status(true, "%s is not a valid .crls file.", name.c_str());
    return false;
  }

  uint64_t ops = get_le(head.subspan<HEADER_OPS_POS, 8>()), mem = get_le(head.subspan<HEADER_MEM_POS, 8>());
  uint64_t offset = get_le(head.subspan(pointer_pos_).first<8>());
  uint64_t length = get_le(head.subspan(pointer_pos_ + 8).first<8>());
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
  std::array<unsigned char, SIGN_PK_BYTES> pk;
  if(password && !derive_keys(password, header, pk)){
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  unsigned long long plain_size;
  bool ok = crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &plain_size, nullptr,
                                                       blob.data() + NONCE_BYTES, length - NONCE_BYTES,
                                                       header.data(), ad_len_, blob.data(), key_.data()) == 0;
  entries_.clear();
  if(ok && !parse_index(plain.span().first(static_cast<std::size_t>(plain_size)), total)){
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

  std::ranges::copy(blob.span().first(NONCE_BYTES), index_nonce_.begin());
  std::ranges::copy(head.first(ad_len_), header_ad_.begin());
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
  std::array<unsigned char, MAX_HEADER_BYTES> header{};
  std::span<unsigned char> head(header);
  std::ranges::copy(MAGIC, header.begin());
  header[HEADER_VERSION_POS] = 3;
  header[HEADER_ALGORITHM_POS] = 1;
  put_le(head.subspan<HEADER_OPS_POS, 8>(), ops);
  put_le(head.subspan<HEADER_MEM_POS, 8>(), mem);
  randombytes_buf(header.data() + HEADER_SALT_POS, SALT_BYTES);

  if(!derive_keys(password, header, head.subspan<HEADER_PK_POS, SIGN_PK_BYTES>())){
    fd.reset();
    unlink(path.c_str());
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  std::ranges::copy(head.first(ad_len_), header_ad_.begin());
  if(!write_at(fd.get(), head.first(header_bytes_), 0) || !write_index(fd.get())){
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
  return std::transform_reduce(entries_.begin(), entries_.end(), uint64_t{0}, std::plus<>(),
                               [](const archive_entry &e){ return e.size; });
}

uint64_t archive::wasted_space() const{
  uint64_t used = std::transform_reduce(entries_.begin(), entries_.end(), header_bytes_ + index_length_,
                                        std::plus<>(), [](const archive_entry &e){ return e.stored; });
  uint64_t total = src_ ? src_->size() : 0;
  return total > used ? total - used : 0;
}

bool has_crls_ext(std::string_view name){
  return name.size() > 5 && name.ends_with(".crls");
}

/* ---------------- Modifications ---------------- */

std::optional<archive_entry> archive::encrypt_file(const std::string &path, int fd, uint64_t position){
  std::string_view name = fs_basename(path);
  if(name.empty() || name.size() > MAX_NAME){
    set_status(true, "Invalid file name.");
    return std::nullopt;
  }

  archive_entry e;
  e.name = name;
  e.offset = position;
  if(find(name)){
    set_status(true, "There is already a file named %s in the .crls.", e.name.c_str());
    return std::nullopt;
  }

  unique_file source(fopen(path.c_str(), "rb"));
  struct stat info;
  if(!source || fstat(fileno(source.get()), &info) != 0 || !S_ISREG(info.st_mode)){
    set_status(true, "Could not read %s.", path.c_str());
    return std::nullopt;
  }
  if(fd_ && fs_same_file(fd_.get(), fileno(source.get()))){
    set_status(true, "A .crls cannot be added to itself.");
    return std::nullopt;
  }

  crypto_secretstream_xchacha20poly1305_state state;
  crypto_secretstream_xchacha20poly1305_init_push(&state, e.stream_header.data(), key_.data());

  bool ok = true, final = false;
  while(ok && !final){
    size_t n = fread(plain_chunk.data(), 1, plain_chunk.size(), source.get());
    if(ferror(source.get())){
      ok = false;
      break;
    }
    final = n < CHUNK;
    unsigned char tag = final ? crypto_secretstream_xchacha20poly1305_tag_final()
                              : crypto_secretstream_xchacha20poly1305_tag_message();
    unsigned long long cipher_size;
    crypto_secretstream_xchacha20poly1305_push(&state, cipher_chunk.data(), &cipher_size, plain_chunk.data(), n,
                                               nullptr, 0, tag);
    ok = write_at(fd, std::span(cipher_chunk).first(static_cast<size_t>(cipher_size)), position);
    position += cipher_size;
    e.size += n;
  }
  plain_chunk.wipe();
  sodium_memzero(&state, sizeof(state));
  e.stored = position - e.offset;

  if(!ok){
    set_status(true, "Could not add %s: %s", e.name.c_str(), strerror(errno));
    return std::nullopt;
  }
  return e;
}

bool archive::add_file(const std::string &path){
  uint64_t end = file_size(fd_.get());
  std::optional<archive_entry> e = encrypt_file(path, fd_.get(), end);
  if(!e){
    fs_truncate(fd_.get(), end);
    return false;
  }

  entries_.push_back(*e);
  if(!write_index(fd_.get())){
    entries_.pop_back();
    fs_truncate(fd_.get(), e->offset);
    return false;
  }

  set_status(false, "Added %s (%s).", e->name.c_str(), format_size(e->size).c_str());
  return true;
}

void archive::record_append(const archive_entry &e, uint64_t index_offset, uint64_t index_length,
                            std::span<const unsigned char, NONCE_BYTES> nonce){
  entries_.push_back(e);
  index_offset_ = index_offset;
  index_length_ = index_length;
  std::ranges::copy(nonce, index_nonce_.begin());
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
  const std::vector<archive_entry> saved_entries = entries_;
  const std::array<unsigned char, MAX_AD_BYTES> saved_ad = header_ad_;
  const int saved_version = version_;
  const uint64_t saved_index_offset = index_offset_, saved_index_length = index_length_;
  secret<KEY_BYTES> saved_key;
  secret<SIGN_SK_BYTES> saved_sk;
  std::ranges::copy(key_.span(), saved_key.span().begin());
  std::ranges::copy(sign_sk_.span(), saved_sk.span().begin());
  auto restore = [&]{
    entries_ = saved_entries;
    std::ranges::copy(saved_key.span(), key_.span().begin());
    std::ranges::copy(saved_sk.span(), sign_sk_.span().begin());
    header_ad_ = saved_ad;
    set_layout(saved_version);
    index_offset_ = saved_index_offset;
    index_length_ = saved_index_length;
  };

  /* A version 2 archive is converted: its key is the master key of version 3. */
  bool upgrade = version_ < 3;
  std::array<unsigned char, MAX_HEADER_BYTES> header{};
  std::span<unsigned char> head(header);
  std::ranges::copy(header_ad_.begin(), header_ad_.begin() + ad_len_, header.begin());
  if(upgrade){
    header[HEADER_VERSION_POS] = 3;
    set_layout(3);
    derive_v3_keys(saved_key.span(), head.subspan<HEADER_PK_POS, SIGN_PK_BYTES>());
    std::ranges::copy(head.first(ad_len_), header_ad_.begin());
  }
  bool ok = write_at(fd.get(), head.first(header_bytes_), 0);

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
        crypto_secretstream_xchacha20poly1305_push(&push_state, out_chunk.data(), &cipher_size, plain.data(),
                                                   plain.size(), nullptr, 0, tag);
        bool sent = write_at(fd.get(), std::span(out_chunk).first(static_cast<size_t>(cipher_size)), written);
        written += cipher_size;
        return sent;
      });
      sodium_memzero(&push_state, sizeof(push_state));
      ok = result == pull_result::ok && written - position == e.stored;
      if(ok) e.stream_header = stream_header;
      else if(result != pull_result::ok) report_pull_error(e, result, tmp_path, remote_, path_);
    }else{
      for(uint64_t copied = 0; ok && copied < e.stored; ){
        std::span<unsigned char> chunk =
            std::span(cipher_chunk).first(static_cast<size_t>(std::min<uint64_t>(e.stored - copied, cipher_chunk.size())));
        ok = src_->read_at(chunk, e.offset + copied) && write_at(fd.get(), chunk, position + copied);
        copied += chunk.size();
      }
    }
    e.offset = position;
    position += e.stored;
  }

  ok = ok && write_index(fd.get()) && fs_replace_file(tmp_path, fd, path_, fd_);

  if(!ok){
    restore();
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
  if(!fs_is_plain_name(e.name)){
    set_status(true, "Name not allowed in the .crls: %s", e.name.c_str());
    return extract_result::failed;
  }

  std::string path = join_path(dir, e.name);
  unique_fd out(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600));
  if(!out){
    if(errno == EEXIST) return extract_result::skipped;
    set_status(true, "Could not create %s: %s", path.c_str(), strerror(errno));
    return extract_result::failed;
  }

  uint64_t written = 0;
  pull_result result = pull_entry(*src_, e, key_.data(), [&](std::span<const unsigned char> plain, bool){
    bool ok = write_at(out.get(), plain, written);
    written += plain.size();
    return ok;
  });
  if(::close(out.release()) != 0 && result == pull_result::ok) result = pull_result::sink_error;
  if(result != pull_result::ok){
    unlink(path.c_str());
    report_pull_error(e, result, path, remote_, path_);
    return extract_result::failed;
  }
  return extract_result::extracted;
}

}
