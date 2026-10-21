/**
 * @file    util.h
 * @brief   General-purpose helpers, independent of the .crls format, the UI and
 *          the operating system (see fs.h for the latter).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section util_overview Overview
 *
 * - put_le() / get_le(): fixed-width little-endian integers in a span.
 * - byte_writer / byte_reader: sequential serialization over a span, used by
 *   the index and the upload message. They never write or read past the end
 *   of their span: an overflow sets a sticky failure flag instead.
 * - ASCII helpers for case-insensitive comparisons (HTTP headers, file names).
 * - format_size() and join_path() for text shown to the user.
 */

#ifndef CORE_UTIL_H
#define CORE_UTIL_H

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace crls{

/**
 * @brief Encodes value as an N-byte little-endian integer.
 * @param out   Destination of exactly N bytes.
 * @param value Value to encode (only its N low bytes are kept).
 */
template<std::size_t N>
constexpr void put_le(std::span<unsigned char, N> out, uint64_t value) noexcept{
  static_assert(N >= 1 && N <= 8, "put_le() writes from 1 to 8 bytes");
  for(std::size_t i = 0; i < N; i++) out[i] = static_cast<unsigned char>(value >> (8*i));
}

/**
 * @brief  Decodes an N-byte little-endian integer.
 * @param  in Source of exactly N bytes.
 * @return Decoded value.
 */
template<class Byte, std::size_t N>
  requires std::same_as<std::remove_const_t<Byte>, unsigned char>
constexpr uint64_t get_le(std::span<Byte, N> in) noexcept{
  static_assert(N >= 1 && N <= 8, "get_le() reads from 1 to 8 bytes");
  uint64_t value = 0;
  for(std::size_t i = 0; i < N; i++) value |= static_cast<uint64_t>(in[i]) << (8*i);
  return value;
}

/**
 * @brief Writes integers and bytes one after another into a span.
 *
 * A write that does not fit is dropped and marks the writer as failed, so a
 * caller that sized the span correctly only needs to check the result once.
 */
class byte_writer{
public:
  /** @brief Starts writing at the beginning of out. */
  explicit byte_writer(std::span<unsigned char> out) noexcept : out_(out) {}

  /**
   * @brief Appends value as an N-byte little-endian integer.
   * @param value Value to write.
   */
  template<std::size_t N>
  void le(uint64_t value) noexcept{
    if(!fits(N)) return;
    put_le(out_.subspan(pos_).first<N>(), value);
    pos_ += N;
  }

  /**
   * @brief Appends a run of bytes.
   * @param data Bytes to write.
   */
  void bytes(std::span<const unsigned char> data) noexcept{
    if(!fits(data.size())) return;
    std::ranges::copy(data, out_.begin() + static_cast<std::ptrdiff_t>(pos_));
    pos_ += data.size();
  }

  /**
   * @brief Appends the characters of text as bytes.
   * @param text Characters to write.
   */
  void bytes(std::string_view text) noexcept{
    bytes(std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
  }

  /** @brief Number of bytes written so far. */
  std::size_t written() const noexcept{ return pos_; }

  /** @brief True if every write fitted. */
  explicit operator bool() const noexcept{ return ok_; }

private:
  /**
   * @brief  Checks that n more bytes fit, marking the writer as failed if not.
   * @param  n Number of bytes.
   * @return True if they fit.
   */
  bool fits(std::size_t n) noexcept{
    if(ok_ && n > out_.size() - pos_) ok_ = false;
    return ok_;
  }

  std::span<unsigned char> out_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

/**
 * @brief Reads integers and bytes one after another from a span.
 *
 * A read past the end returns zero (or an empty span), consumes nothing and
 * marks the reader as failed, so a parser checks the result once per record.
 */
class byte_reader{
public:
  /** @brief Starts reading at the beginning of in. */
  explicit byte_reader(std::span<const unsigned char> in) noexcept : in_(in) {}

  /**
   * @brief  Reads an N-byte little-endian integer.
   * @return The value, or 0 if fewer than N bytes are left.
   */
  template<std::size_t N>
  uint64_t le() noexcept{
    if(!fits(N)) return 0;
    uint64_t value = get_le(in_.first<N>());
    in_ = in_.subspan(N);
    return value;
  }

  /**
   * @brief  Reads a run of bytes.
   * @param  n Number of bytes.
   * @return A view of the bytes, or an empty span if fewer than n are left.
   */
  std::span<const unsigned char> bytes(std::size_t n) noexcept{
    if(!fits(n)) return {};
    std::span<const unsigned char> out = in_.first(n);
    in_ = in_.subspan(n);
    return out;
  }

  /**
   * @brief Reads exactly out.size() bytes into out (left untouched if fewer
   *        are left).
   * @param out Destination.
   */
  void copy_to(std::span<unsigned char> out) noexcept{
    std::span<const unsigned char> in = bytes(out.size());
    if(in.size() == out.size()) std::ranges::copy(in, out.begin());
  }

  /** @brief Number of bytes not read yet. */
  std::size_t remaining() const noexcept{ return in_.size(); }

  /** @brief True if every read was complete. */
  explicit operator bool() const noexcept{ return ok_; }

private:
  /**
   * @brief  Checks that n more bytes are left, marking the reader as failed if not.
   * @param  n Number of bytes.
   * @return True if they are left.
   */
  bool fits(std::size_t n) noexcept{
    if(ok_ && n > in_.size()) ok_ = false;
    return ok_;
  }

  std::span<const unsigned char> in_;
  bool ok_ = true;
};

/**
 * @brief  Lowercases an ASCII character.
 * @param  c Character.
 * @return c in lowercase; other characters are returned unchanged.
 */
constexpr char ascii_lower(char c) noexcept{
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/**
 * @brief  Compares two strings case-insensitively (ASCII only).
 * @param  a First string.
 * @param  b Second string.
 * @return True if they are equal.
 */
constexpr bool equals_nocase(std::string_view a, std::string_view b) noexcept{
  return std::ranges::equal(a, b, {}, ascii_lower, ascii_lower);
}

/**
 * @brief  Checks case-insensitively (ASCII only) whether s starts with prefix.
 * @param  s      String.
 * @param  prefix Prefix.
 * @return True if s starts with prefix.
 */
constexpr bool starts_with_nocase(std::string_view s, std::string_view prefix) noexcept{
  return s.size() >= prefix.size() && equals_nocase(s.substr(0, prefix.size()), prefix);
}

/**
 * @brief  Orders two strings case-insensitively (ASCII only).
 * @param  a First string.
 * @param  b Second string.
 * @return True if a sorts before b.
 */
constexpr bool less_nocase(std::string_view a, std::string_view b) noexcept{
  return std::ranges::lexicographical_compare(a, b, {}, ascii_lower, ascii_lower);
}

/**
 * @brief  Removes spaces and tabs from both ends.
 * @param  s String.
 * @return The trimmed slice.
 */
constexpr std::string_view trim(std::string_view s) noexcept{
  std::size_t first = s.find_first_not_of(" \t");
  if(first == std::string_view::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

/**
 * @brief  Formats a size in human-readable form, e.g. "512 B" or "1.5 MB".
 * @param  size Size in bytes.
 * @return The text.
 */
std::string format_size(uint64_t size);

/**
 * @brief  Builds dir + "/" + name, without duplicating the separator when dir
 *         already ends with one (e.g. "/" or "C:\\").
 * @param  dir  Directory path.
 * @param  name Entry name.
 * @return The joined path.
 */
std::string join_path(std::string_view dir, std::string_view name);

}

#endif
