/**
 * @file    sodium.hpp
 * @brief   Minimal hand-written libsodium declarations.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section sodium_overview Overview
 *
 * These declarations allow the program to link against libsodium.so.23
 * without the libsodium-dev headers. Each signature matches the one in the
 * official header. archive_crypto_init() verifies at startup that the sizes
 * assumed here match those reported by the library.
 */

#ifndef DEPS_SODIUM_HPP
#define DEPS_SODIUM_HPP

#include <stddef.h>
#include <stdint.h>

/* The library is written in C: its symbols are not mangled. */
extern "C"{

/**
 * @brief Opaque secretstream state; its layout matches the official header.
 */
typedef struct{
  unsigned char k[32];
  unsigned char nonce[12];
  unsigned char _pad[8];
}crypto_secretstream_xchacha20poly1305_state;

/**
 * @brief Opaque BLAKE2b state; its size and alignment match the official header.
 */
typedef struct{
  alignas(64) unsigned char opaque[384];
}crypto_generichash_state;

int    sodium_init(void);
void   sodium_memzero(void *pnt, size_t len);
void   randombytes_buf(void *buf, size_t size);
int    sodium_memcmp(const void *b1, const void *b2, size_t len);
int    crypto_pwhash(unsigned char *out, unsigned long long outlen, const char *passwd,
                     unsigned long long passwdlen, const unsigned char *salt,
                     unsigned long long opslimit, size_t memlimit, int alg);
int    crypto_pwhash_alg_argon2id13(void);
size_t crypto_pwhash_opslimit_moderate(void);
size_t crypto_pwhash_memlimit_moderate(void);
size_t crypto_pwhash_saltbytes(void);
size_t crypto_aead_xchacha20poly1305_ietf_keybytes(void);
size_t crypto_aead_xchacha20poly1305_ietf_npubbytes(void);
size_t crypto_aead_xchacha20poly1305_ietf_abytes(void);
int    crypto_aead_xchacha20poly1305_ietf_encrypt(unsigned char *c, unsigned long long *clen,
                                                  const unsigned char *m, unsigned long long mlen,
                                                  const unsigned char *ad, unsigned long long adlen,
                                                  const unsigned char *nsec, const unsigned char *npub,
                                                  const unsigned char *k);
int    crypto_aead_xchacha20poly1305_ietf_decrypt(unsigned char *m, unsigned long long *mlen,
                                                  unsigned char *nsec,
                                                  const unsigned char *c, unsigned long long clen,
                                                  const unsigned char *ad, unsigned long long adlen,
                                                  const unsigned char *npub, const unsigned char *k);
size_t crypto_kdf_keybytes(void);
size_t crypto_kdf_contextbytes(void);
int    crypto_kdf_derive_from_key(unsigned char *subkey, size_t subkey_len, uint64_t subkey_id,
                                  const char ctx[8], const unsigned char key[32]);
size_t crypto_sign_seedbytes(void);
size_t crypto_sign_publickeybytes(void);
size_t crypto_sign_secretkeybytes(void);
size_t crypto_sign_bytes(void);
int    crypto_sign_seed_keypair(unsigned char *pk, unsigned char *sk, const unsigned char *seed);
int    crypto_sign_detached(unsigned char *sig, unsigned long long *siglen_p,
                            const unsigned char *m, unsigned long long mlen, const unsigned char *sk);
int    crypto_sign_verify_detached(const unsigned char *sig, const unsigned char *m,
                                   unsigned long long mlen, const unsigned char *pk);
size_t crypto_generichash_statebytes(void);
int    crypto_generichash_init(crypto_generichash_state *state, const unsigned char *key,
                               const size_t keylen, const size_t outlen);
int    crypto_generichash_update(crypto_generichash_state *state, const unsigned char *in,
                                 unsigned long long inlen);
int    crypto_generichash_final(crypto_generichash_state *state, unsigned char *out, const size_t outlen);
size_t crypto_secretstream_xchacha20poly1305_statebytes(void);
size_t crypto_secretstream_xchacha20poly1305_headerbytes(void);
size_t crypto_secretstream_xchacha20poly1305_abytes(void);
unsigned char crypto_secretstream_xchacha20poly1305_tag_message(void);
unsigned char crypto_secretstream_xchacha20poly1305_tag_final(void);
int    crypto_secretstream_xchacha20poly1305_init_push(crypto_secretstream_xchacha20poly1305_state *state,
                                                       unsigned char *header, const unsigned char *k);
int    crypto_secretstream_xchacha20poly1305_push(crypto_secretstream_xchacha20poly1305_state *state,
                                                  unsigned char *c, unsigned long long *clen,
                                                  const unsigned char *m, unsigned long long mlen,
                                                  const unsigned char *ad, unsigned long long adlen,
                                                  unsigned char tag);
int    crypto_secretstream_xchacha20poly1305_init_pull(crypto_secretstream_xchacha20poly1305_state *state,
                                                       const unsigned char *header, const unsigned char *k);
int    crypto_secretstream_xchacha20poly1305_pull(crypto_secretstream_xchacha20poly1305_state *state,
                                                  unsigned char *m, unsigned long long *mlen,
                                                  unsigned char *tag,
                                                  const unsigned char *c, unsigned long long clen,
                                                  const unsigned char *ad, unsigned long long adlen);

}

#endif
