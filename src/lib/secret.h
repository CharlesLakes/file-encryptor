/**
 * @file    secret.h
 * @brief   Fixed-size buffers for passwords and keys, wiped on destruction.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section secret_overview Overview
 *
 * std::string and a growing std::vector may leave copies of their contents
 * behind (short-string storage, reallocations) that nobody wipes. Secrets are
 * kept in these fixed arrays instead: they never reallocate, cannot be copied,
 * and are wiped with sodium_memzero() when destroyed or when wipe() is called.
 *
 * | Type             | Element         | Use                         |
 * |------------------|-----------------|-----------------------------|
 * | secret<N>        | unsigned char   | Keys                        |
 * | secret_text<N>   | char            | Passwords (NUL-terminated)  |
 */

#ifndef LIB_SECRET_H
#define LIB_SECRET_H

#include <array>
#include <cstddef>
#include <span>
#include "deps/sodium.h"

namespace crls{

/**
 * @brief N elements of type T, zero-initialized and wiped on destruction.
 */
template<class T, std::size_t N>
class basic_secret{
public:
  basic_secret() = default;
  ~basic_secret(){ wipe(); }

  basic_secret(const basic_secret &) = delete;
  basic_secret& operator=(const basic_secret &) = delete;

  /** @brief Pointer to the first element. */
  T* data() noexcept{ return bytes_.data(); }
  const T* data() const noexcept{ return bytes_.data(); }

  /** @brief Number of elements. */
  static constexpr std::size_t size() noexcept{ return N; }

  /** @brief The elements as a span. */
  std::span<T, N> span() noexcept{ return std::span<T, N>(bytes_); }
  std::span<const T, N> span() const noexcept{ return std::span<const T, N>(bytes_); }

  /** @brief Overwrites every element with zeros. */
  void wipe() noexcept{ sodium_memzero(bytes_.data(), sizeof(bytes_)); }

private:
  std::array<T, N> bytes_{};
};

/** @brief Secret bytes, such as a key. */
template<std::size_t N> using secret = basic_secret<unsigned char, N>;

/** @brief Secret characters, such as a password with its NUL terminator. */
template<std::size_t N> using secret_text = basic_secret<char, N>;

}

#endif
