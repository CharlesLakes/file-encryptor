/**
 * @file    source.cpp
 * @brief   Local file implementation of the archive byte source (see source.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/source.h"

#include <string.h>
#include "core/fs.h"

namespace crls{

/** @brief read_at() of a local source. */
static bool file_read_at(archive_source *src, void *buf, size_t len, uint64_t offset){
  return read_at(src->fd, buf, len, offset);
}

/** @brief size() of a local source. */
static uint64_t file_source_size(archive_source *src){
  return file_size(src->fd);
}

void source_from_fd(archive_source *src, int fd){
  memset(src, 0, sizeof(*src));
  src->read_at = file_read_at;
  src->size = file_source_size;
  src->fd = fd;
}

void source_close(archive_source *src){
  if(src->close) src->close(src);
  memset(src, 0, sizeof(*src));
  src->fd = -1;
}

}
