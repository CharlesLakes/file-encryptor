/**
 * @file    fs_posix.cpp
 * @brief   POSIX implementation of the file system abstraction (see fs.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/fs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

namespace crls{

bool read_at(int fd, void *buf, size_t len, uint64_t offset){
  unsigned char *p = (unsigned char *) buf;
  while(len > 0){
    ssize_t n = pread(fd, p, len, (off_t) offset);
    if(n <= 0) return false;
    p += n;
    len -= (size_t) n;
    offset += (uint64_t) n;
  }
  return true;
}

bool write_at(int fd, const void *buf, size_t len, uint64_t offset){
  const unsigned char *p = (const unsigned char *) buf;
  while(len > 0){
    ssize_t n = pwrite(fd, p, len, (off_t) offset);
    if(n <= 0) return false;
    p += n;
    len -= (size_t) n;
    offset += (uint64_t) n;
  }
  return true;
}

uint64_t file_size(int fd){
  struct stat info;
  return fstat(fd, &info) == 0 ? (uint64_t) info.st_size : 0;
}

bool fs_lock(int fd){
  return flock(fd, LOCK_EX | LOCK_NB) == 0;
}

bool fs_sync(int fd){
  return fsync(fd) == 0;
}

bool fs_truncate(int fd, uint64_t size){
  return ftruncate(fd, (off_t) size) == 0;
}

bool fs_same_file(int fd_a, int fd_b){
  struct stat a, b;
  return fstat(fd_a, &a) == 0 && fstat(fd_b, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool fs_absolute_path(const std::string &path, std::string &out){
  char resolved[PATH_MAX];
  if(!realpath(path.c_str(), resolved)) return false;
  out = resolved;
  return true;
}

bool fs_replace_file(const std::string &from, unique_fd &from_fd, const std::string &to, unique_fd &to_fd){
  /* POSIX allows renaming over a file that is still open. */
  if(rename(from.c_str(), to.c_str()) != 0) return false;
  to_fd = std::move(from_fd);
  fs_lock(to_fd.get());
  return true;
}

unique_fd fs_open_regular(const std::string &path, bool write){
  /* O_NONBLOCK keeps a FIFO from blocking the open; it is cleared afterwards. */
  unique_fd fd(open(path.c_str(), (write ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_NONBLOCK | O_BINARY));
  struct stat info;
  if(!fd || fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode)) return unique_fd();
  fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) & ~O_NONBLOCK);
  return fd;
}

bool fs_file_id(int fd, char *out, size_t out_size){
  struct stat info;
  if(fstat(fd, &info) != 0) return false;
  snprintf(out, out_size, "%llx-%llx", (unsigned long long) info.st_dev, (unsigned long long) info.st_ino);
  return true;
}

unique_fd fs_temp_file(){
  const char *dir = getenv("TMPDIR");
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/crls-XXXXXX", dir && dir[0] ? dir : "/tmp");
  unique_fd fd(mkstemp(path));
  /* The file lives on, nameless, until the descriptor is closed. */
  if(fd) unlink(path);
  return fd;
}

bool fs_is_separator(char c){
  return c == '/';
}

bool fs_is_absolute(const char *path){
  return path[0] == '/';
}

bool fs_is_root(const char *path){
  return strcmp(path, "/") == 0;
}

const char* fs_basename(const char *path){
  const char *name = strrchr(path, '/');
  return name ? name + 1 : path;
}

bool fs_is_plain_name(const char *name){
  return name[0] != '\0' && strchr(name, '/') == NULL && strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

}
