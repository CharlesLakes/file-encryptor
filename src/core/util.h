/**
 * @file    util.h
 * @brief   General-purpose helpers, independent of the .crls format and the UI.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#ifndef CORE_UTIL_H
#define CORE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encodes value as a little-endian integer.
 * @param p     Destination buffer (at least bytes long).
 * @param value Value to encode.
 * @param bytes Number of bytes to write, from 1 to 8.
 */
void     put_le(unsigned char *p, uint64_t value, int bytes);

/**
 * @brief  Decodes a little-endian integer.
 * @param  p     Source buffer (at least bytes long).
 * @param  bytes Number of bytes to read, from 1 to 8.
 * @return Decoded value.
 */
uint64_t get_le(const unsigned char *p, int bytes);

/**
 * @brief Formats a size in human-readable form, e.g. "512 B" or "1.5 MB".
 * @param size     Size in bytes.
 * @param out      Destination buffer.
 * @param out_size Capacity of out.
 */
void     format_size(uint64_t size, char *out, size_t out_size);

/**
 * @brief  Reads exactly len bytes at offset.
 * @param  fd     File descriptor.
 * @param  buf    Destination buffer.
 * @param  len    Number of bytes to read.
 * @param  offset Position in the file.
 * @return True on success, false on any error or premature end of file.
 */
bool     read_at(int fd, void *buf, size_t len, uint64_t offset);

/**
 * @brief  Writes exactly len bytes at offset.
 * @param  fd     File descriptor.
 * @param  buf    Source buffer.
 * @param  len    Number of bytes to write.
 * @param  offset Position in the file.
 * @return True on success, false on any error.
 */
bool     write_at(int fd, const void *buf, size_t len, uint64_t offset);

/**
 * @brief  Returns the size of an open file.
 * @param  fd File descriptor.
 * @return Size in bytes, or 0 if it cannot be determined.
 */
uint64_t file_size(int fd);

/**
 * @brief Builds dir + "/" + name, without duplicating the slash when dir is "/".
 * @param out      Destination buffer.
 * @param out_size Capacity of out.
 * @param dir      Directory path.
 * @param name     Entry name.
 */
void     join_path(char *out, size_t out_size, const char *dir, const char *name);

#endif
