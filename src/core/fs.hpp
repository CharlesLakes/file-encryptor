/**
 * @file    fs.hpp
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
 * | fs_posix.cpp  | Linux and other POSIX |
 * | fs_win32.cpp  | Windows (MinGW-w64)   |
 *
 * File descriptors are the ones returned by open(). Every call to open() must
 * include O_BINARY, which is defined as 0 on systems that do not need it.
 */

#ifndef CORE_FS_HPP
#define CORE_FS_HPP

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "lib/unique_fd.hpp"

/* A macro, because it is combined with the O_* flags of <fcntl.h>. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace crls{

/**
 * @brief  Fills buf with the bytes at offset.
 * @param  fd     File descriptor.
 * @param  buf    Destination; exactly buf.size() bytes are read.
 * @param  offset Position in the file.
 * @return True on success, false on any error or premature end of file.
 */
[[nodiscard]] bool read_at(int fd, std::span<unsigned char> buf, uint64_t offset);

/**
 * @brief  Writes all of buf at offset.
 * @param  fd     File descriptor.
 * @param  buf    Bytes to write.
 * @param  offset Position in the file.
 * @return True on success, false on any error.
 */
[[nodiscard]] bool write_at(int fd, std::span<const unsigned char> buf, uint64_t offset);

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
 * @return The absolute path, or std::nullopt (with errno set) on failure.
 */
std::optional<std::string> fs_absolute_path(const std::string &path);

/**
 * @brief  Returns the current working folder.
 * @return Its absolute path, or std::nullopt if it cannot be determined.
 */
std::optional<std::string> fs_current_dir();

/**
 * @brief  Checks whether anything (a file, a folder, a link...) exists at path.
 * @param  path Path.
 * @return True if it exists.
 */
bool        fs_exists(const std::string &path);

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
 *         by archive::compact(), but not when it is modified in place.
 * @param  fd File descriptor.
 * @return Short hexadecimal text, or std::nullopt on failure.
 */
std::optional<std::string> fs_file_id(int fd);

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
bool        fs_is_absolute(std::string_view path);

/**
 * @brief  Checks whether an absolute path is the root of its file system.
 * @param  path Absolute path, e.g. "/" or "C:\\".
 * @return True if path has no parent folder, false otherwise.
 */
bool        fs_is_root(std::string_view path);

/**
 * @brief  Returns the last component of a path.
 * @param  path Path.
 * @return The part of path after its last separator, or path itself.
 */
std::string_view fs_basename(std::string_view path);

/**
 * @brief  Checks that name is a single path component that cannot escape the
 *         folder it is created in.
 * @param  name File name.
 * @return True if the name is safe, false otherwise.
 */
bool        fs_is_plain_name(std::string_view name);

}

#endif
