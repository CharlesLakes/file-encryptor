/**
 * @file    sodium.h
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

#ifndef DEPS_SODIUM_H
#define DEPS_SODIUM_H

#include <stddef.h>

/**
 * @brief Opaque secretstream state; its layout matches the official header.
 */
typedef struct{
  unsigned char k[32];
  unsigned char nonce[12];
  unsigned char _pad[8];
}crypto_secretstream_xchacha20poly1305_state;

int    sodium_init(void);
void   sodium_memzero(void *pnt, size_t len);
void   randombytes_buf(void *buf, size_t size);
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

#endif
