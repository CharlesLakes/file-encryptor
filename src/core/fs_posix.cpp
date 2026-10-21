/**
 * @file    fs_posix.cpp
 * @brief   POSIX implementation of the file system abstraction (see fs.hpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/fs.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

namespace crls{

/** @brief Deleter of a string allocated with malloc() by the C library. */
struct c_free{
  void operator()(char *p) const noexcept{ free(p); }
};

/** @brief A malloc()ed string that is freed when it goes out of scope. */
using c_string = std::unique_ptr<char, c_free>;

bool read_at(int fd, std::span<unsigned char> buf, uint64_t offset){
  while(!buf.empty()){
    ssize_t n = pread(fd, buf.data(), buf.size(), static_cast<off_t>(offset));
    if(n <= 0) return false;
    buf = buf.subspan(static_cast<std::size_t>(n));
    offset += static_cast<uint64_t>(n);
  }
  return true;
}

bool write_at(int fd, std::span<const unsigned char> buf, uint64_t offset){
  while(!buf.empty()){
    ssize_t n = pwrite(fd, buf.data(), buf.size(), static_cast<off_t>(offset));
    if(n <= 0) return false;
    buf = buf.subspan(static_cast<std::size_t>(n));
    offset += static_cast<uint64_t>(n);
  }
  return true;
}

uint64_t file_size(int fd){
  struct stat info;
  return fstat(fd, &info) == 0 ? static_cast<uint64_t>(info.st_size) : 0;
}

bool fs_lock(int fd){
  return flock(fd, LOCK_EX | LOCK_NB) == 0;
}

bool fs_sync(int fd){
  return fsync(fd) == 0;
}

bool fs_truncate(int fd, uint64_t size){
  return ftruncate(fd, static_cast<off_t>(size)) == 0;
}

bool fs_same_file(int fd_a, int fd_b){
  struct stat a, b;
  return fstat(fd_a, &a) == 0 && fstat(fd_b, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

std::optional<std::string> fs_absolute_path(const std::string &path){
  /* With a null buffer, realpath() allocates the result with malloc(). */
  c_string resolved(realpath(path.c_str(), nullptr));
  if(!resolved) return std::nullopt;
  return std::string(resolved.get());
}

std::optional<std::string> fs_current_dir(){
  c_string cwd(getcwd(nullptr, 0));
  if(!cwd) return std::nullopt;
  return std::string(cwd.get());
}

bool fs_exists(const std::string &path){
  return access(path.c_str(), F_OK) == 0;
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

/**
 * @brief  Formats a number in hexadecimal.
 * @param  value Number.
 * @return Lowercase digits, without a prefix.
 */
static std::string to_hex(uint64_t value){
  std::array<char, 16> digits;
  auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
  return std::string(digits.data(), end);
}

std::optional<std::string> fs_file_id(int fd){
  struct stat info;
  if(fstat(fd, &info) != 0) return std::nullopt;
  return to_hex(static_cast<uint64_t>(info.st_dev)) + "-" + to_hex(static_cast<uint64_t>(info.st_ino));
}

unique_fd fs_temp_file(){
  const char *dir = getenv("TMPDIR");
  std::string path = std::string(dir && dir[0] ? dir : "/tmp") + "/crls-XXXXXX";
  unique_fd fd(mkstemp(path.data()));
  /* The file lives on, nameless, until the descriptor is closed. */
  if(fd) unlink(path.c_str());
  return fd;
}

bool fs_is_separator(char c){
  return c == '/';
}

bool fs_is_absolute(std::string_view path){
  return path.starts_with('/');
}

bool fs_is_root(std::string_view path){
  return path == "/";
}

std::string_view fs_basename(std::string_view path){
  std::size_t slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool fs_is_plain_name(std::string_view name){
  /* A NUL byte would cut the name short when it reaches open(). */
  return !name.empty() && name.find_first_of(std::string_view("/\0", 2)) == std::string_view::npos &&
         name != "." && name != "..";
}

}
