/**
 * @file    unique_fd.h
 * @brief   Owning wrapper of a file descriptor that closes it on destruction.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section unique_fd_usage Usage
 *
 * @code
 * crls::unique_fd fd(open(path, O_RDONLY | O_BINARY));
 * if(!fd) return false;            // open() failed
 * read_at(fd.get(), buf, len, 0);  // the descriptor is closed when fd goes out of scope
 * @endcode
 *
 * Move-only, like std::unique_ptr. release() gives up ownership without
 * closing.
 */

#ifndef LIB_UNIQUE_FD_H
#define LIB_UNIQUE_FD_H

#include <unistd.h>

namespace crls{

/**
 * @brief Owns a file descriptor (-1 when empty).
 */
class unique_fd{
public:
  unique_fd() = default;

  /** @brief Takes ownership of fd, which may be -1. */
  explicit unique_fd(int fd) noexcept : fd_(fd) {}

  ~unique_fd(){ reset(); }

  unique_fd(const unique_fd &) = delete;
  unique_fd& operator=(const unique_fd &) = delete;

  unique_fd(unique_fd &&other) noexcept : fd_(other.release()) {}

  unique_fd& operator=(unique_fd &&other) noexcept{
    if(this != &other) reset(other.release());
    return *this;
  }

  /** @brief Returns the descriptor without giving up ownership. */
  int get() const noexcept{ return fd_; }

  /** @brief Gives up ownership and returns the descriptor. */
  int release() noexcept{
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

  /** @brief Closes the current descriptor, if any, and takes ownership of fd. */
  void reset(int fd = -1) noexcept{
    if(fd_ >= 0) ::close(fd_);
    fd_ = fd;
  }

  /** @brief True if a descriptor is owned. */
  explicit operator bool() const noexcept{ return fd_ >= 0; }

private:
  int fd_ = -1;
};

}

#endif
