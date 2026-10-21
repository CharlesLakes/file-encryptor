/**
 * @file    source.cpp
 * @brief   Local file implementation of the archive byte source (see source.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/source.h"

#include "core/fs.h"

namespace crls{

bool fd_source::read_at(std::span<unsigned char> buf, uint64_t offset){
  return crls::read_at(fd_, buf.data(), buf.size(), offset);
}

uint64_t fd_source::size() const{
  return file_size(fd_);
}

}
