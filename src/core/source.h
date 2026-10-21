/**
 * @file    source.h
 * @brief   Read-only byte source behind an archive: a local file or a remote URL.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section source_overview Overview
 *
 * archive.c reads the header, the index and the file contents only through an
 * archive_source, so the same code serves local archives and archives fetched
 * over HTTP (see remote.h). Writing is not part of this interface: local
 * archives write through their file descriptor and remote archives upload
 * appends (see append.h).
 */

#ifndef CORE_SOURCE_H
#define CORE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct archive_source archive_source;

/**
 * @brief Byte source with positioned reads.
 */
struct archive_source{
  /** Reads exactly len bytes at offset; false on any error. */
  bool     (*read_at)(archive_source *src, void *buf, size_t len, uint64_t offset);
  /** Total size of the source in bytes. */
  uint64_t (*size)(archive_source *src);
  /** Releases the resources of the source (not called for local files). */
  void     (*close)(archive_source *src);
  int fd;                    /**< File descriptor of a local source, or -1. */
  void *ctx;                 /**< State of a remote source, or NULL. */
};

/**
 * @brief Makes src read from an open local file. The descriptor stays owned by
 *        the caller.
 * @param src Source to initialize.
 * @param fd  File descriptor.
 */
void source_from_fd(archive_source *src, int fd);

/**
 * @brief Releases a source and leaves it empty. Safe on an empty source.
 * @param src Source.
 */
void source_close(archive_source *src);

#endif
