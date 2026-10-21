/**
 * @file    remote.h
 * @brief   Archives published at an http or https URL: reading with Range
 *          requests and adding files with signed uploads.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section remote_overview Overview
 *
 * An archive at a URL is read through an archive_source that fetches byte
 * ranges (see http.h), so opening it downloads only the header and the index,
 * and extracting a file downloads only that file. Any server that supports
 * single Range requests works: the built-in server (server/server.h), nginx,
 * Apache, object storage and so on. Uploading needs the built-in server.
 *
 * Every request after the first one carries If-Match with the ETag of the
 * archive. If the archive is replaced on the server (for example, compacted),
 * the server answers 412 and the archive has to be opened again.
 *
 * As with local archives, nothing is visible without the password: the index
 * only decrypts with the right key, and the server only accepts uploads
 * signed with the key derived from it.
 */

#ifndef CORE_REMOTE_H
#define CORE_REMOTE_H

#include <stdbool.h>
#include "core/archive.h"

#define REMOTE_BLOCK (1u << 20)   /**< Size of the read cache and of each Range request. */

/**
 * @brief  Checks whether s is an http:// or https:// URL.
 * @param  s String.
 * @return True if it starts with one of those schemes (case-insensitive).
 */
bool remote_is_url(const char *s);

/**
 * @brief  Opens the archive at a URL.
 * @param  a        Closed archive.
 * @param  url      http or https URL of a .crls file.
 * @param  password Password used to derive the key.
 * @return True on success, false otherwise (the status explains why).
 */
bool remote_open(archive *a, const char *url, const char *password);

/**
 * @brief  Encrypts a local file and uploads it to the archive, retrying on top
 *         of newer states if other clients upload at the same time. Only the
 *         new file and the new index are sent.
 * @param  a    Archive opened with remote_open().
 * @param  path Local file to add.
 * @return True on success, false otherwise (the status explains why).
 */
bool remote_add_file(archive *a, const char *path);

#endif
