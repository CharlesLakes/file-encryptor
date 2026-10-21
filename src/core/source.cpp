/**
 * @file    source.cpp
 * @brief   Local file implementation of the archive byte source (see source.hpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/source.hpp"

#include "core/fs.hpp"

namespace crls{

bool fd_source::read_at(std::span<unsigned char> buf, uint64_t offset){
  return crls::read_at(fd_, buf, offset);
}

uint64_t fd_source::size() const{
  return file_size(fd_);
}

}
