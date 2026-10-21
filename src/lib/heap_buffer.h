/**
 * @file    heap_buffer.h
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
 * read_at(fd, blob.data(), blob.size(), offset);
 * @endcode
 */

#ifndef LIB_HEAP_BUFFER_H
#define LIB_HEAP_BUFFER_H

#include <cstddef>
#include <new>
#include <span>
#include "deps/sodium.h"

namespace crls{

/**
 * @brief An owned heap array of bytes (empty until allocate() succeeds).
 */
class heap_buffer{
public:
  heap_buffer() = default;
  ~heap_buffer(){ release(); }

  heap_buffer(const heap_buffer &) = delete;
  heap_buffer& operator=(const heap_buffer &) = delete;

  heap_buffer(heap_buffer &&other) noexcept : data_(other.data_), size_(other.size_), wipe_(other.wipe_){
    other.data_ = nullptr;
    other.size_ = 0;
  }

  heap_buffer& operator=(heap_buffer &&other) noexcept{
    if(this != &other){
      release();
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
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

  /** @brief Frees the contents, wiping them first for a secret_buffer. */
  void release() noexcept{
    if(data_ && wipe_) sodium_memzero(data_, size_);
    delete[] data_;
    data_ = nullptr;
    size_ = 0;
  }

  unsigned char* data() noexcept{ return data_; }
  const unsigned char* data() const noexcept{ return data_; }
  std::size_t size() const noexcept{ return size_; }
  std::span<unsigned char> span() noexcept{ return {data_, size_}; }
  std::span<const unsigned char> span() const noexcept{ return {data_, size_}; }

protected:
  /** @brief Used by secret_buffer. */
  explicit heap_buffer(bool wipe) noexcept : wipe_(wipe) {}

private:
  unsigned char *data_ = nullptr;
  std::size_t size_ = 0;
  bool wipe_ = false;
};

/**
 * @brief A heap_buffer that wipes its contents before freeing them.
 */
class secret_buffer : public heap_buffer{
public:
  secret_buffer() noexcept : heap_buffer(true) {}
};

}

#endif
