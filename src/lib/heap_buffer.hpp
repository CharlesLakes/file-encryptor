/**
 * @file    heap_buffer.hpp
 * @brief   Heap byte buffers whose allocation may fail without aborting.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section heap_buffer_overview Overview
 *
 * The project is built without exceptions, so a failed allocation inside
 * std::vector or std::string ends the program. That is acceptable for small
 * sizes, but not for sizes read from a file or the network (an index of up to
 * 64 MB, a download block): those use heap_buffer, whose allocate() returns
 * false instead, so the caller can report "Not enough memory".
 *
 * secret_buffer is the same but wipes its contents before freeing them.
 *
 * @code
 * crls::heap_buffer blob;
 * if(!blob.allocate(length)){
 *   set_status(true, "Not enough memory.");
 *   return false;
 * }
 * read_at(fd, blob.span(), offset);
 * @endcode
 */

#ifndef LIB_HEAP_BUFFER_HPP
#define LIB_HEAP_BUFFER_HPP

#include <cstddef>
#include <new>
#include <span>
#include <utility>
#include "deps/sodium.hpp"

namespace crls{

/**
 * @brief  An owned heap array of bytes (empty until allocate() succeeds).
 * @tparam Wipe True to wipe the contents with sodium_memzero() before they
 *         are freed. heap_buffer and secret_buffer are separate types, so a
 *         secret can never be moved into a buffer that would not wipe it.
 */
template<bool Wipe>
class basic_heap_buffer{
public:
  basic_heap_buffer() = default;
  ~basic_heap_buffer(){ release(); }

  basic_heap_buffer(const basic_heap_buffer &) = delete;
  basic_heap_buffer& operator=(const basic_heap_buffer &) = delete;

  basic_heap_buffer(basic_heap_buffer &&other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

  basic_heap_buffer& operator=(basic_heap_buffer &&other) noexcept{
    if(this != &other){
      release();
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  /**
   * @brief  Replaces the contents with size zero bytes.
   * @param  size Number of bytes (0 allocates an empty, valid buffer).
   * @return True on success, false if there is not enough memory (the
   *         buffer is then empty).
   */
  [[nodiscard]] bool allocate(std::size_t size) noexcept{
    release();
    data_ = new (std::nothrow) unsigned char[size ? size : 1]();
    if(!data_) return false;
    size_ = size;
    return true;
  }

  /** @brief Frees the contents, wiping them first if Wipe is true. */
  void release() noexcept{
    if constexpr(Wipe){
      if(data_) sodium_memzero(data_, size_);
    }
    delete[] data_;
    data_ = nullptr;
    size_ = 0;
  }

  unsigned char* data() noexcept{ return data_; }
  const unsigned char* data() const noexcept{ return data_; }
  std::size_t size() const noexcept{ return size_; }
  std::span<unsigned char> span() noexcept{ return {data_, size_}; }
  std::span<const unsigned char> span() const noexcept{ return {data_, size_}; }

private:
  unsigned char *data_ = nullptr;
  std::size_t size_ = 0;
};

/** @brief A heap buffer for data that is not secret. */
using heap_buffer = basic_heap_buffer<false>;

/** @brief A heap buffer that wipes its contents before freeing them. */
using secret_buffer = basic_heap_buffer<true>;

}

#endif
