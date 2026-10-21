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
 * | secret_string<N> | char            | Passwords being typed       |
 */

#ifndef LIB_SECRET_H
#define LIB_SECRET_H

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
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

/**
 * @brief Text of up to Capacity characters that grows and shrinks in place,
 *        such as a password being typed. It is kept NUL-terminated in a
 *        secret_text, so it never reallocates and is wiped on destruction.
 */
template<std::size_t Capacity>
class secret_string{
public:
  /**
   * @brief  Appends a character.
   * @param  c Character.
   * @return True if it fitted, false if the text is already full.
   */
  bool push_back(char c) noexcept{
    if(length_ == Capacity) return false;
    text_.data()[length_++] = c;
    text_.data()[length_] = '\0';
    return true;
  }

  /** @brief Removes the last character, if any. */
  void pop_back() noexcept{
    if(length_ > 0) text_.data()[--length_] = '\0';
  }

  /** @brief Overwrites the text with zeros and empties it. */
  void wipe() noexcept{
    text_.wipe();
    length_ = 0;
  }

  /** @brief The characters, without the terminator. */
  std::string_view view() const noexcept{ return {text_.data(), length_}; }

  /** @brief The characters as a NUL-terminated string. */
  const char* c_str() const noexcept{ return text_.data(); }

  /** @brief Number of characters. */
  std::size_t size() const noexcept{ return length_; }

  /** @brief True if there are no characters. */
  bool empty() const noexcept{ return length_ == 0; }

  /** @brief Largest number of characters. */
  static constexpr std::size_t capacity() noexcept{ return Capacity; }

private:
  secret_text<Capacity + 1> text_;
  std::size_t length_ = 0;
};

}

#endif
