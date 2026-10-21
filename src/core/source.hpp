/**
 * @file    source.hpp
 * @brief   Read-only byte source behind an archive: a local file or a remote URL.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section source_overview Overview
 *
 * archive.cpp reads the header, the index and the file contents only through a
 * byte_source, so the same code serves local archives (fd_source) and archives
 * fetched over HTTP (remote_source, see remote.cpp). Writing is not part of
 * this interface: local archives write through their file descriptor and
 * remote archives upload appends (see append.hpp).
 */

#ifndef CORE_SOURCE_HPP
#define CORE_SOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace crls{

class remote_source;

/**
 * @brief Byte source with positioned reads.
 */
class byte_source{
public:
  virtual ~byte_source() = default;

  /**
   * @brief  Reads exactly buf.size() bytes at offset.
   * @param  buf    Destination.
   * @param  offset Position in the source.
   * @return True on success, false on any error (a remote source sets the
   *         status message).
   */
  [[nodiscard]] virtual bool read_at(std::span<unsigned char> buf, uint64_t offset) = 0;

  /** @brief Total size of the source in bytes. */
  virtual uint64_t size() const = 0;

  /** @brief The remote source behind this object, or nullptr for a local one. */
  virtual remote_source* as_remote() noexcept{ return nullptr; }
};

/**
 * @brief Reads from an open local file. The descriptor is not owned.
 */
class fd_source final : public byte_source{
public:
  /** @brief Reads from fd, which must stay open while this object is used. */
  explicit fd_source(int fd) noexcept : fd_(fd) {}

  bool read_at(std::span<unsigned char> buf, uint64_t offset) override;
  uint64_t size() const override;

private:
  int fd_;
};

}

#endif
