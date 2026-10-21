/**
 * @file    fs.h
 * @brief   Operating system abstraction for files and paths.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section fs_overview Overview
 *
 * Gathers every file system operation whose behavior differs between POSIX
 * systems and Windows. The rest of the core only uses these functions, so it
 * builds unchanged on both. The implementation is selected at build time:
 *
 * | File          | Platform              |
 * |---------------|-----------------------|
 * | fs_posix.c    | Linux and other POSIX |
 * | fs_win32.c    | Windows (MinGW-w64)   |
 *
 * File descriptors are the ones returned by open(). Every call to open() must
 * include O_BINARY, which is defined as 0 on systems that do not need it.
 */

#ifndef CORE_FS_H
#define CORE_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <string>
#include "lib/unique_fd.h"

namespace crls{

#ifndef O_BINARY
#define O_BINARY 0
#endif

/**
 * @brief  Reads exactly len bytes at offset.
 * @param  fd     File descriptor.
 * @param  buf    Destination buffer.
 * @param  len    Number of bytes to read.
 * @param  offset Position in the file.
 * @return True on success, false on any error or premature end of file.
 */
bool        read_at(int fd, void *buf, size_t len, uint64_t offset);

/**
 * @brief  Writes exactly len bytes at offset.
 * @param  fd     File descriptor.
 * @param  buf    Source buffer.
 * @param  len    Number of bytes to write.
 * @param  offset Position in the file.
 * @return True on success, false on any error.
 */
bool        write_at(int fd, const void *buf, size_t len, uint64_t offset);

/**
 * @brief  Returns the size of an open file.
 * @param  fd File descriptor.
 * @return Size in bytes, or 0 if it cannot be determined.
 */
uint64_t    file_size(int fd);

/**
 * @brief  Acquires a non-blocking exclusive lock on the whole file.
 * @param  fd File descriptor.
 * @return True if the lock was acquired, false otherwise.
 */
bool        fs_lock(int fd);

/**
 * @brief  Flushes the file contents to the storage device.
 * @param  fd File descriptor.
 * @return True on success, false otherwise.
 */
bool        fs_sync(int fd);

/**
 * @brief  Truncates or extends the file to size bytes.
 * @param  fd   File descriptor.
 * @param  size New size in bytes.
 * @return True on success, false otherwise.
 */
bool        fs_truncate(int fd, uint64_t size);

/**
 * @brief  Checks whether two descriptors refer to the same file.
 * @param  fd_a First file descriptor.
 * @param  fd_b Second file descriptor.
 * @return True if both refer to the same file, false otherwise or on error.
 */
bool        fs_same_file(int fd_a, int fd_b);

/**
 * @brief  Resolves path into an absolute, normalized path.
 * @param  path Path to resolve (an existing file or folder).
 * @param  out  Output: the absolute path (may be the same object as path).
 * @return True on success, false otherwise.
 */
bool        fs_absolute_path(const std::string &path, std::string &out);

/**
 * @brief  Replaces the file at to with the file at from.
 *
 * On success to_fd holds a locked read-write descriptor of the replaced file
 * and from_fd is empty; the previous descriptors are closed. On failure both
 * files are left untouched and both descriptors remain open, except in the
 * unlikely case where the replacement succeeded but the file could not be
 * reopened: then both are empty.
 *
 * @param  from    Path of the new file.
 * @param  from_fd Descriptor of the new file.
 * @param  to      Path of the file to replace.
 * @param  to_fd   Descriptor of the file to replace.
 * @return True on success, false otherwise.
 */
bool        fs_replace_file(const std::string &from, unique_fd &from_fd, const std::string &to, unique_fd &to_fd);

/**
 * @brief  Opens an existing regular file, refusing symbolic links (or reparse
 *         points), folders, devices and pipes.
 * @param  path  Path of the file.
 * @param  write True for read-write access, false for read-only.
 * @return The descriptor, empty on error.
 */
unique_fd   fs_open_regular(const std::string &path, bool write);

/**
 * @brief  Describes the identity of an open file (device and inode, or volume
 *         and file index). It changes when the file is replaced, for example
 *         by archive_compact(), but not when it is modified in place.
 * @param  fd       File descriptor.
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out (64 bytes are enough).
 * @return True on success, false otherwise.
 */
bool        fs_file_id(int fd, char *out, size_t out_size);

/**
 * @brief  Creates an anonymous temporary file, deleted when it is closed.
 * @return Read-write descriptor, empty on error.
 */
unique_fd   fs_temp_file();

/**
 * @brief  Checks whether c separates path components.
 * @param  c Character.
 * @return True for '/' (and '\\' on Windows), false otherwise.
 */
bool        fs_is_separator(char c);

/**
 * @brief  Checks whether a path is absolute.
 * @param  path Path.
 * @return True for "/x" (and "C:\\x", "C:/x" or "\\\\server\\x" on Windows).
 */
bool        fs_is_absolute(const char *path);

/**
 * @brief  Checks whether an absolute path is the root of its file system.
 * @param  path Absolute path, e.g. "/" or "C:\\".
 * @return True if path has no parent folder, false otherwise.
 */
bool        fs_is_root(const char *path);

/**
 * @brief  Returns the last component of a path.
 * @param  path Path.
 * @return Pointer into path just after its last separator, or path itself.
 */
const char* fs_basename(const char *path);

/**
 * @brief  Checks that name is a single path component that cannot escape the
 *         folder it is created in.
 * @param  name File name.
 * @return True if the name is safe, false otherwise.
 */
bool        fs_is_plain_name(const char *name);

}

#endif
