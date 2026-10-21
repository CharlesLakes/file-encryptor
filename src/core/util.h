/**
 * @file    util.h
 * @brief   General-purpose helpers, independent of the .crls format, the UI and
 *          the operating system (see fs.h for the latter).
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
 * @brief Builds dir + "/" + name, without duplicating the separator when dir
 *        already ends with one (e.g. "/" or "C:\\").
 * @param out      Destination buffer.
 * @param out_size Capacity of out.
 * @param dir      Directory path.
 * @param name     Entry name.
 */
void     join_path(char *out, size_t out_size, const char *dir, const char *name);

#endif
