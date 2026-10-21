/**
 * @file    archive.c
 * @brief   Implementation of the .crls archive (see archive.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/archive.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

/** @brief Shared scratch buffers; only one chunk is processed at a time. */
static unsigned char plain_chunk[CHUNK];
static unsigned char cipher_chunk[CHUNK + STREAM_ABYTES];

/* ---------------- Helpers ---------------- */

/**
 * @brief  Computes the encrypted size of a file of the given plaintext size.
 *
 * A file occupies size / CHUNK + 1 chunks; the last chunk may be empty.
 *
 * @param  size Plaintext size in bytes.
 * @return Encrypted size in bytes.
 */
static uint64_t stored_size(uint64_t size){
  return size + (size / CHUNK + 1) * STREAM_ABYTES;
}

/**
 * @brief Removes all entries from the in-memory index.
 * @param a Archive.
 */
static void clear_entries(archive *a){
  free_vector(&a->entries);
  a->entries = create_vector((unsigned int) sizeof(archive_entry));
}

/**
 * @brief  Derives the archive key from the password with Argon2id.
 * @param  a        Archive whose key is written.
 * @param  password Password.
 * @param  salt     Salt of SALT_BYTES bytes.
 * @param  ops      Argon2id operations limit.
 * @param  mem      Argon2id memory limit.
 * @return True on success, false if the derivation fails (e.g. out of memory).
 */
static bool derive_key(archive *a, const char *password, const unsigned char *salt, uint64_t ops, uint64_t mem){
  return crypto_pwhash(a->key, KEY_BYTES, password, strlen(password), salt,
                       ops, (size_t) mem, crypto_pwhash_alg_argon2id13()) == 0;
}

/**
 * @brief Stores the absolute path of the archive, or path as given if it
 *        cannot be resolved.
 * @param a    Archive.
 * @param path Path of the archive.
 */
static void remember_path(archive *a, const char *path){
  if(!fs_absolute_path(path, a->path)) snprintf(a->path, sizeof(a->path), "%s", path);
}

/**
 * @brief  Looks up an entry by name.
 * @param  a    Archive.
 * @param  name File name.
 * @return Index of the entry, or -1 if not found.
 */
static int find_entry(archive *a, const char *name){
  for(unsigned int i = 0; i < a->entries.n; i++)
    if(strcmp(archive_get_entry(a, i)->name, name) == 0) return (int) i;
  return -1;
}

/**
 * @brief Sets the layout fields of a for the given format version.
 * @param a       Archive.
 * @param version Format version.
 */
static void set_layout(archive *a, int version){
  a->version = version;
  a->header_bytes = V2_HEADER_BYTES;
  a->ad_len = V2_AD_BYTES;
  a->pointer_pos = V2_AD_BYTES;
}

/* ---------------- Index ---------------- */

/**
 * @brief  Encrypts the index, appends it to the end of fd and then updates
 *         the header to point to it.
 * @param  a  Archive.
 * @param  fd Destination file descriptor.
 * @return True on success, false otherwise.
 */
static bool write_index_to(archive *a, int fd){
  uint64_t plain_size = 4;
  for(unsigned int i = 0; i < a->entries.n; i++)
    plain_size += 2 + strlen(archive_get_entry(a, i)->name) + 8 + 8 + 8 + STREAM_HEADER;
  if(plain_size > MAX_INDEX){
    set_status(true, "There are too many files in the .crls.");
    return false;
  }

  uint64_t blob_size = NONCE_BYTES + plain_size + TAG_BYTES;
  unsigned char *plain = (unsigned char *) malloc(plain_size);
  unsigned char *blob = (unsigned char *) malloc(blob_size);
  if(!plain || !blob){
    free(plain);
    free(blob);
    set_status(true, "Not enough memory to save the index.");
    return false;
  }

  unsigned char *p = plain;
  put_le(p, a->entries.n, 4);
  p += 4;
  for(unsigned int i = 0; i < a->entries.n; i++){
    archive_entry *e = archive_get_entry(a, i);
    size_t name_len = strlen(e->name);
    put_le(p, name_len, 2);
    p += 2;
    memcpy(p, e->name, name_len);
    p += name_len;
    put_le(p, e->size, 8);
    put_le(p + 8, e->offset, 8);
    put_le(p + 16, e->stored, 8);
    p += 24;
    memcpy(p, e->stream_header, STREAM_HEADER);
    p += STREAM_HEADER;
  }

  randombytes_buf(blob, NONCE_BYTES);
  crypto_aead_xchacha20poly1305_ietf_encrypt(blob + NONCE_BYTES, NULL, plain, plain_size,
                                             a->header_ad, a->ad_len, NULL, blob, a->key);
  sodium_memzero(plain, plain_size);
  free(plain);

  uint64_t end = file_size(fd);
  unsigned char pointer[16];
  put_le(pointer, end, 8);
  put_le(pointer + 8, blob_size, 8);

  bool ok = write_at(fd, blob, blob_size, end) && fs_sync(fd) &&
            write_at(fd, pointer, sizeof(pointer), a->pointer_pos) && fs_sync(fd);
  free(blob);

  if(!ok){
    set_status(true, "Could not write the index: %s", strerror(errno));
    fs_truncate(fd, end);
    return false;
  }
  a->index_offset = end;
  a->index_length = blob_size;
  return true;
}

/**
 * @brief  Parses a decrypted index and validates every entry against the
 *         archive size.
 * @param  a          Archive whose entries are filled.
 * @param  p          Decrypted index.
 * @param  size       Length of the decrypted index.
 * @param  total_size Size of the archive file.
 * @return True if the index is well formed, false otherwise.
 */
static bool parse_index(archive *a, const unsigned char *p, uint64_t size, uint64_t total_size){
  const unsigned char *end = p + size;
  if(size < 4) return false;
  uint64_t count = get_le(p, 4);
  p += 4;

  for(uint64_t i = 0; i < count; i++){
    archive_entry e;
    if(end - p < 2) return false;
    size_t name_len = (size_t) get_le(p, 2);
    p += 2;
    if(name_len == 0 || name_len > MAX_NAME || (size_t) (end - p) < name_len) return false;
    memcpy(e.name, p, name_len);
    e.name[name_len] = '\0';
    p += name_len;

    if(end - p < 24 + STREAM_HEADER) return false;
    e.size = get_le(p, 8);
    e.offset = get_le(p + 8, 8);
    e.stored = get_le(p + 16, 8);
    p += 24;
    memcpy(e.stream_header, p, STREAM_HEADER);
    p += STREAM_HEADER;

    if(e.size > total_size || e.stored != stored_size(e.size) || e.offset < a->header_bytes ||
       e.offset > total_size || e.stored > total_size - e.offset) return false;
    push_back_vector(&a->entries, (void *) &e);
  }
  return p == end;
}

/* ---------------- Lifecycle ---------------- */

bool archive_crypto_init(void){
  return sodium_init() >= 0 && crypto_pwhash_saltbytes() == SALT_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_keybytes() == KEY_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_npubbytes() == NONCE_BYTES &&
         crypto_aead_xchacha20poly1305_ietf_abytes() == TAG_BYTES &&
         crypto_secretstream_xchacha20poly1305_headerbytes() == STREAM_HEADER &&
         crypto_secretstream_xchacha20poly1305_abytes() == STREAM_ABYTES &&
         crypto_secretstream_xchacha20poly1305_statebytes() == sizeof(crypto_secretstream_xchacha20poly1305_state);
}

void archive_init(archive *a){
  memset(a, 0, sizeof(*a));
  a->fd = -1;
  a->src.fd = -1;
  a->entries = create_vector((unsigned int) sizeof(archive_entry));
}

void archive_close(archive *a){
  clear_entries(a);
  sodium_memzero(a->key, sizeof(a->key));
  source_close(&a->src);
  if(a->fd >= 0) close(a->fd);
  a->fd = -1;
  a->is_open = false;
  a->path[0] = '\0';
}

void archive_free(archive *a){
  archive_close(a);
  free_vector(&a->entries);
}

/**
 * @brief  Reads the header and the index from a->src and decrypts the index.
 *
 * On success the key, the layout fields, the entries and the index location
 * of a are set. On failure a->key is wiped and the status explains why.
 *
 * @param  a        Archive whose source is ready.
 * @param  name     Name of the archive for error messages.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise.
 */
static bool load_from_source(archive *a, const char *name, const char *password){
  unsigned char header[MAX_HEADER_BYTES];
  uint64_t total = a->src.size(&a->src);
  if(total < V2_HEADER_BYTES || !a->src.read_at(&a->src, header, V2_HEADER_BYTES, 0) ||
     memcmp(header, MAGIC, MAGIC_BYTES) != 0){
    set_status(true, "%s is not a valid .crls file.", name);
    return false;
  }
  if(header[6] != 2 || header[7] != 1){
    set_status(true, "Unsupported .crls version (%d).", header[6]);
    return false;
  }
  set_layout(a, header[6]);

  uint64_t ops = get_le(header + 8, 8), mem = get_le(header + 16, 8);
  uint64_t offset = get_le(header + a->pointer_pos, 8), length = get_le(header + a->pointer_pos + 8, 8);
  if(ops < 1 || ops > MAX_OPSLIMIT || mem < 8192 || mem > MAX_MEMLIMIT ||
     offset < a->header_bytes || offset > total || length < NONCE_BYTES + TAG_BYTES + 4 ||
     length > NONCE_BYTES + MAX_INDEX + TAG_BYTES || length > total - offset){
    set_status(true, "The .crls header is damaged.");
    return false;
  }

  unsigned char *blob = (unsigned char *) malloc(length);
  unsigned char *plain = (unsigned char *) malloc(length);
  if(!blob || !plain || !a->src.read_at(&a->src, blob, length, offset)){
    free(blob);
    free(plain);
    set_status(true, "Could not read the .crls index.");
    return false;
  }

  if(!derive_key(a, password, header + 24, ops, mem)){
    free(blob);
    free(plain);
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  unsigned long long plain_size;
  bool ok = crypto_aead_xchacha20poly1305_ietf_decrypt(plain, &plain_size, NULL,
                                                       blob + NONCE_BYTES, length - NONCE_BYTES,
                                                       header, a->ad_len, blob, a->key) == 0;
  free(blob);
  clear_entries(a);
  if(ok && !parse_index(a, plain, plain_size, total)){
    clear_entries(a);
    ok = false;
    set_status(true, "The decrypted index is corrupt.");
  }else if(!ok){
    set_status(true, "Wrong password or damaged file.");
  }
  sodium_memzero(plain, length);
  free(plain);

  if(!ok){
    sodium_memzero(a->key, sizeof(a->key));
    return false;
  }

  memcpy(a->header_ad, header, a->ad_len);
  a->index_offset = offset;
  a->index_length = length;
  return true;
}

bool archive_open(archive *a, const char *path, const char *password){
  int fd = open(path, O_RDWR | O_BINARY);
  if(fd < 0){
    set_status(true, "Could not open %s: %s", path, strerror(errno));
    return false;
  }
  if(!fs_lock(fd)){
    close(fd);
    set_status(true, "%s is already open in another program.", path);
    return false;
  }

  source_from_fd(&a->src, fd);
  if(!load_from_source(a, path, password)){
    source_close(&a->src);
    close(fd);
    return false;
  }

  a->fd = fd;
  a->is_open = true;
  remember_path(a, path);
  return true;
}

bool archive_create(archive *a, const char *path, const char *password){
  return archive_create_params(a, path, password, crypto_pwhash_opslimit_moderate(),
                               crypto_pwhash_memlimit_moderate());
}

bool archive_create_params(archive *a, const char *path, const char *password, uint64_t ops, uint64_t mem){
  status_error = false;
  int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
  if(fd < 0){
    set_status(true, "Could not create %s: %s", path, strerror(errno));
    return false;
  }
  fs_lock(fd);

  set_layout(a, 2);
  unsigned char header[MAX_HEADER_BYTES] = {0};
  memcpy(header, MAGIC, MAGIC_BYTES);
  header[6] = 2;
  header[7] = 1;
  put_le(header + 8, ops, 8);
  put_le(header + 16, mem, 8);
  randombytes_buf(header + 24, SALT_BYTES);

  if(!derive_key(a, password, header + 24, ops, mem)){
    close(fd);
    unlink(path);
    set_status(true, "Not enough memory to derive the key.");
    return false;
  }

  clear_entries(a);
  memcpy(a->header_ad, header, a->ad_len);
  if(!write_at(fd, header, a->header_bytes, 0) || !write_index_to(a, fd)){
    if(!status_error) set_status(true, "Could not write %s: %s", path, strerror(errno));
    sodium_memzero(a->key, sizeof(a->key));
    close(fd);
    unlink(path);
    return false;
  }

  a->fd = fd;
  source_from_fd(&a->src, fd);
  a->is_open = true;
  remember_path(a, path);
  return true;
}

/* ---------------- Queries ---------------- */

archive_entry* archive_get_entry(archive *a, unsigned int index){
  return (archive_entry *) get_value_vector(&a->entries, index);
}

uint64_t archive_total_size(archive *a){
  uint64_t total = 0;
  for(unsigned int i = 0; i < a->entries.n; i++) total += archive_get_entry(a, i)->size;
  return total;
}

uint64_t archive_wasted_space(archive *a){
  uint64_t used = a->header_bytes + a->index_length;
  for(unsigned int i = 0; i < a->entries.n; i++) used += archive_get_entry(a, i)->stored;
  uint64_t total = a->src.size(&a->src);
  return total > used ? total - used : 0;
}

bool has_crls_ext(const char *name){
  size_t len = strlen(name);
  return len > 5 && strcmp(name + len - 5, ".crls") == 0;
}

/* ---------------- Modifications ---------------- */

bool archive_add_file(archive *a, const char *path){
  const char *name = fs_basename(path);
  if(strlen(name) == 0 || strlen(name) > MAX_NAME){
    set_status(true, "Invalid file name.");
    return false;
  }
  if(find_entry(a, name) >= 0){
    set_status(true, "There is already a file named %s in the .crls.", name);
    return false;
  }

  FILE *source = fopen(path, "rb");
  struct stat info;
  if(!source || fstat(fileno(source), &info) != 0 || !S_ISREG(info.st_mode)){
    if(source) fclose(source);
    set_status(true, "Could not read %s.", path);
    return false;
  }
  if(fs_same_file(a->fd, fileno(source))){
    fclose(source);
    set_status(true, "A .crls cannot be added to itself.");
    return false;
  }

  archive_entry e;
  strcpy(e.name, name);
  e.size = 0;
  e.offset = file_size(a->fd);

  crypto_secretstream_xchacha20poly1305_state state;
  crypto_secretstream_xchacha20poly1305_init_push(&state, e.stream_header, a->key);

  uint64_t position = e.offset;
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
    ok = write_at(a->fd, cipher_chunk, (size_t) cipher_size, position);
    position += cipher_size;
    e.size += n;
  }
  fclose(source);
  sodium_memzero(plain_chunk, sizeof(plain_chunk));
  sodium_memzero(&state, sizeof(state));
  e.stored = position - e.offset;

  if(!ok){
    fs_truncate(a->fd, e.offset);
    set_status(true, "Could not add %s: %s", name, strerror(errno));
    return false;
  }

  push_back_vector(&a->entries, (void *) &e);
  if(!write_index_to(a, a->fd)){
    a->entries.n--;
    fs_truncate(a->fd, e.offset);
    return false;
  }

  char size_text[32];
  format_size(e.size, size_text, sizeof(size_text));
  set_status(false, "Added %s (%s).", e.name, size_text);
  return true;
}

bool archive_delete_entry(archive *a, unsigned int index){
  unsigned int size = a->entries.byte_size;
  archive_entry removed = *archive_get_entry(a, index);
  char *base = (char *) a->entries.arr;
  memmove(base + size*index, base + size*(index+1), size*(a->entries.n - index - 1));
  a->entries.n--;

  if(!write_index_to(a, a->fd)){
    /* Restore the entry at its original position. */
    push_back_vector(&a->entries, (void *) &removed);
    base = (char *) a->entries.arr;
    memmove(base + size*(index+1), base + size*index, size*(a->entries.n - index - 1));
    memcpy(base + size*index, &removed, size);
    return false;
  }

  set_status(false, "Deleted %s. Use Compact to reclaim the space.", removed.name);
  return true;
}

bool archive_compact(archive *a){
  status_error = false;
  char tmp_path[PATH_MAX + 8];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", a->path);
  int fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
  if(fd < 0){
    set_status(true, "Could not create %s: %s", tmp_path, strerror(errno));
    return false;
  }

  unsigned char header[MAX_HEADER_BYTES] = {0};
  memcpy(header, a->header_ad, a->ad_len);
  bool ok = write_at(fd, header, a->header_bytes, 0);

  uint64_t position = a->header_bytes;
  uint64_t *old_offsets = (uint64_t *) malloc(sizeof(uint64_t) * (a->entries.n ? a->entries.n : 1));
  ok = ok && old_offsets;
  for(unsigned int i = 0; ok && i < a->entries.n; i++){
    archive_entry *e = archive_get_entry(a, i);
    old_offsets[i] = e->offset;
    for(uint64_t copied = 0; ok && copied < e->stored; ){
      size_t n = e->stored - copied < sizeof(cipher_chunk) ? (size_t) (e->stored - copied) : sizeof(cipher_chunk);
      ok = a->src.read_at(&a->src, cipher_chunk, n, e->offset + copied) &&
           write_at(fd, cipher_chunk, n, position + copied);
      copied += n;
    }
    e->offset = position;
    position += e->stored;
  }

  uint64_t old_index_offset = a->index_offset, old_index_length = a->index_length;
  ok = ok && write_index_to(a, fd) && fs_replace_file(tmp_path, &fd, a->path, &a->fd);

  if(!ok){
    for(unsigned int i = 0; old_offsets && i < a->entries.n; i++) archive_get_entry(a, i)->offset = old_offsets[i];
    a->index_offset = old_index_offset;
    a->index_length = old_index_length;
    free(old_offsets);
    if(fd >= 0) close(fd);
    unlink(tmp_path);
    if(a->fd < 0){
      archive_close(a);
      set_status(true, "Could not reopen the archive after compacting.");
      return false;
    }
    /* On Windows the descriptor may have been reopened. */
    source_from_fd(&a->src, a->fd);
    if(!status_error) set_status(true, "Could not compact: %s", strerror(errno));
    return false;
  }

  free(old_offsets);
  source_from_fd(&a->src, a->fd);
  return true;
}

/* ---------------- Extraction ---------------- */

int archive_extract_entry(archive *a, unsigned int index, const char *dir){
  archive_entry *e = archive_get_entry(a, index);
  if(!fs_is_plain_name(e->name)){
    set_status(true, "Name not allowed in the .crls: %s", e->name);
    return -1;
  }

  char path[PATH_MAX];
  join_path(path, sizeof(path), dir, e->name);
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
  if(fd < 0){
    if(errno == EEXIST) return 0;
    set_status(true, "Could not create %s: %s", path, strerror(errno));
    return -1;
  }

  crypto_secretstream_xchacha20poly1305_state state;
  bool ok = crypto_secretstream_xchacha20poly1305_init_pull(&state, e->stream_header, a->key) == 0;
  bool final = false, damaged = !ok;
  uint64_t consumed = 0, written = 0;

  while(ok && !final){
    uint64_t left = e->stored - consumed;
    size_t n = left < sizeof(cipher_chunk) ? (size_t) left : sizeof(cipher_chunk);
    unsigned long long plain_size;
    unsigned char tag;
    if(n < STREAM_ABYTES || !a->src.read_at(&a->src, cipher_chunk, n, e->offset + consumed) ||
       crypto_secretstream_xchacha20poly1305_pull(&state, plain_chunk, &plain_size, &tag,
                                                  cipher_chunk, n, NULL, 0) != 0){
      ok = false;
      damaged = true;
      break;
    }
    consumed += n;
    final = tag == crypto_secretstream_xchacha20poly1305_tag_final();
    if(final != (consumed == e->stored)){
      ok = false;
      damaged = true;
      break;
    }
    ok = write_at(fd, plain_chunk, (size_t) plain_size, written);
    written += plain_size;
  }
  sodium_memzero(plain_chunk, sizeof(plain_chunk));
  sodium_memzero(&state, sizeof(state));

  ok = close(fd) == 0 && ok && written == e->size;
  if(!ok){
    unlink(path);
    if(damaged) set_status(true, "The content of %s is damaged.", e->name);
    else set_status(true, "Could not write %s.", path);
    return -1;
  }
  return 1;
}
