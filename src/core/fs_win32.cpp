/**
 * @file    fs_win32.cpp
 * @brief   Windows implementation of the file system abstraction (see fs.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section fs_win32_notes Notes
 *
 * - Positioned reads and writes use ReadFile()/WriteFile() with an OVERLAPPED
 *   offset on the handle behind each C runtime descriptor.
 * - Locks are mandatory on Windows: other processes cannot read or write the
 *   archive while it is open, but the descriptor holding the lock can.
 * - An open file cannot be renamed or replaced, so fs_replace_file() closes
 *   both descriptors, moves the file and reopens it.
 */

#include "core/fs.h"

#include <errno.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace crls{

/** @brief Largest transfer passed to a single ReadFile() or WriteFile() call. */
#define MAX_TRANSFER (1u << 30)

/**
 * @brief  Returns the Windows handle behind a C runtime descriptor.
 * @param  fd File descriptor.
 * @return Handle, or INVALID_HANDLE_VALUE.
 */
static HANDLE handle_of(int fd){
  return (HANDLE) _get_osfhandle(fd);
}

/**
 * @brief  Builds an OVERLAPPED structure pointing at offset.
 * @param  offset Position in the file.
 * @return Initialized structure.
 */
static OVERLAPPED at_offset(uint64_t offset){
  OVERLAPPED ov;
  memset(&ov, 0, sizeof(ov));
  ov.Offset = (DWORD) offset;
  ov.OffsetHigh = (DWORD) (offset >> 32);
  return ov;
}

bool read_at(int fd, void *buf, size_t len, uint64_t offset){
  HANDLE h = handle_of(fd);
  unsigned char *p = (unsigned char *) buf;
  while(len > 0){
    DWORD want = len < MAX_TRANSFER ? (DWORD) len : MAX_TRANSFER, got = 0;
    OVERLAPPED ov = at_offset(offset);
    if(!ReadFile(h, p, want, &got, &ov) || got == 0) return false;
    p += got;
    len -= got;
    offset += got;
  }
  return true;
}

bool write_at(int fd, const void *buf, size_t len, uint64_t offset){
  HANDLE h = handle_of(fd);
  const unsigned char *p = (const unsigned char *) buf;
  while(len > 0){
    DWORD want = len < MAX_TRANSFER ? (DWORD) len : MAX_TRANSFER, put = 0;
    OVERLAPPED ov = at_offset(offset);
    if(!WriteFile(h, p, want, &put, &ov) || put == 0){
      errno = EIO;
      return false;
    }
    p += put;
    len -= put;
    offset += put;
  }
  return true;
}

uint64_t file_size(int fd){
  LARGE_INTEGER size;
  return GetFileSizeEx(handle_of(fd), &size) ? (uint64_t) size.QuadPart : 0;
}

bool fs_lock(int fd){
  OVERLAPPED ov = at_offset(0);
  return LockFileEx(handle_of(fd), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                    MAXDWORD, MAXDWORD, &ov) != 0;
}

bool fs_sync(int fd){
  return FlushFileBuffers(handle_of(fd)) != 0;
}

bool fs_truncate(int fd, uint64_t size){
  return _chsize_s(fd, (long long) size) == 0;
}

bool fs_same_file(int fd_a, int fd_b){
  BY_HANDLE_FILE_INFORMATION a, b;
  return GetFileInformationByHandle(handle_of(fd_a), &a) && GetFileInformationByHandle(handle_of(fd_b), &b) &&
         a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
         a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

bool fs_absolute_path(const char *path, char *out){
  if(!_fullpath(out, path, PATH_MAX) || GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES){
    errno = ENOENT;
    return false;
  }
  /* Drop trailing separators, except the one of a drive root such as "C:\". */
  size_t len = strlen(out);
  while(len > 1 && fs_is_separator(out[len - 1]) && !fs_is_root(out)) out[--len] = '\0';
  return true;
}

/**
 * @brief  Opens a file for reading and writing, optionally locking it.
 * @param  path Path of the file.
 * @param  lock True to acquire the exclusive lock.
 * @return File descriptor, or -1 on error.
 */
static int reopen(const char *path, bool lock){
  int fd = open(path, O_RDWR | O_BINARY);
  if(fd >= 0 && lock) fs_lock(fd);
  return fd;
}

bool fs_replace_file(const char *from, int *from_fd, const char *to, int *to_fd){
  close(*from_fd);
  close(*to_fd);
  bool moved = MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

  if(!moved){
    errno = EACCES;
    *to_fd = reopen(to, true);
    *from_fd = reopen(from, false);
    return false;
  }
  *from_fd = -1;
  *to_fd = reopen(to, true);
  return *to_fd >= 0;
}

int fs_open_regular(const char *path, bool write){
  /* FILE_SHARE_DELETE lets the GUI compact (replace) the file while it is open here. */
  HANDLE h = CreateFileA(path, GENERIC_READ | (write ? GENERIC_WRITE : 0),
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if(h == INVALID_HANDLE_VALUE) return -1;
  BY_HANDLE_FILE_INFORMATION info;
  if(!GetFileInformationByHandle(h, &info) || GetFileType(h) != FILE_TYPE_DISK ||
     (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))){
    CloseHandle(h);
    return -1;
  }
  int fd = _open_osfhandle((intptr_t) h, (write ? _O_RDWR : _O_RDONLY) | _O_BINARY);
  if(fd < 0) CloseHandle(h);
  return fd;
}

bool fs_file_id(int fd, char *out, size_t out_size){
  BY_HANDLE_FILE_INFORMATION info;
  if(!GetFileInformationByHandle(handle_of(fd), &info)) return false;
  snprintf(out, out_size, "%lx-%lx%08lx", (unsigned long) info.dwVolumeSerialNumber,
           (unsigned long) info.nFileIndexHigh, (unsigned long) info.nFileIndexLow);
  return true;
}

int fs_temp_file(void){
  char dir[MAX_PATH], path[MAX_PATH];
  if(!GetTempPathA(sizeof(dir), dir) || !GetTempFileNameA(dir, "crl", 0, path)) return -1;
  /* _O_TEMPORARY deletes the file when the last descriptor is closed. */
  return open(path, O_RDWR | O_TRUNC | O_BINARY | _O_TEMPORARY);
}

bool fs_is_separator(char c){
  return c == '/' || c == '\\';
}

bool fs_is_absolute(const char *path){
  /* "\\x" and "/x" are relative to the current drive, which is good enough here. */
  if(fs_is_separator(path[0])) return true;
  return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
         path[1] == ':' && fs_is_separator(path[2]);
}

bool fs_is_root(const char *path){
  size_t len = strlen(path);
  if(len == 1) return fs_is_separator(path[0]);
  return len == 3 && path[1] == ':' && fs_is_separator(path[2]);
}

const char* fs_basename(const char *path){
  const char *name = path;
  for(const char *p = path; *p; p++)
    if(fs_is_separator(*p) || *p == ':') name = p + 1;
  return name;
}

/**
 * @brief  Checks whether name is a reserved device name such as "CON", "NUL"
 *         or "COM1", with or without an extension ("nul.txt" is the device
 *         too).
 * @param  name File name.
 * @return True if the name refers to a device.
 */
static bool is_device_name(const char *name){
  static const char *devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", NULL};
  size_t stem = strcspn(name, ".");
  char upper[8];
  if(stem >= sizeof(upper)) return false;
  for(size_t i = 0; i < stem; i++) upper[i] = (char) (name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
  upper[stem] = '\0';
  for(int i = 0; devices[i]; i++)
    if(strcmp(upper, devices[i]) == 0) return true;
  return stem == 4 && (strncmp(upper, "COM", 3) == 0 || strncmp(upper, "LPT", 3) == 0) &&
         upper[3] >= '1' && upper[3] <= '9';
}

bool fs_is_plain_name(const char *name){
  size_t len = strlen(name);
  if(len == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
  /* Windows silently strips trailing dots and spaces, so "..." would mean "..". */
  if(name[len - 1] == '.' || name[len - 1] == ' ') return false;
  for(const char *p = name; *p; p++)
    if((unsigned char) *p < 32 || strchr("/\\:*?\"<>|", *p)) return false;
  return !is_device_name(name);
}

}
