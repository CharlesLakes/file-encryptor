/**
 * @file    fs_win32.cpp
 * @brief   Windows implementation of the file system abstraction (see fs.hpp).
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

#include "core/fs.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <memory>
#include <io.h>
#include <windows.h>
#include "core/util.hpp"

namespace crls{

/** @brief Deleter of a string allocated with malloc() by the C library. */
struct c_free{
  void operator()(char *p) const noexcept{ free(p); }
};

/** @brief A malloc()ed string that is freed when it goes out of scope. */
using c_string = std::unique_ptr<char, c_free>;

/** @brief Largest transfer passed to a single ReadFile() or WriteFile() call. */
inline constexpr std::size_t MAX_TRANSFER = 1u << 30;

/**
 * @brief  Returns the Windows handle behind a C runtime descriptor.
 * @param  fd File descriptor.
 * @return Handle, or INVALID_HANDLE_VALUE.
 */
static HANDLE handle_of(int fd){
  return reinterpret_cast<HANDLE>(_get_osfhandle(fd));
}

/**
 * @brief  Builds an OVERLAPPED structure pointing at offset.
 * @param  offset Position in the file.
 * @return Initialized structure.
 */
static OVERLAPPED at_offset(uint64_t offset){
  OVERLAPPED ov{};
  ov.Offset = static_cast<DWORD>(offset);
  ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  return ov;
}

bool read_at(int fd, std::span<unsigned char> buf, uint64_t offset){
  HANDLE h = handle_of(fd);
  while(!buf.empty()){
    DWORD want = static_cast<DWORD>(std::min(buf.size(), MAX_TRANSFER)), got = 0;
    OVERLAPPED ov = at_offset(offset);
    if(!ReadFile(h, buf.data(), want, &got, &ov) || got == 0) return false;
    buf = buf.subspan(got);
    offset += got;
  }
  return true;
}

bool write_at(int fd, std::span<const unsigned char> buf, uint64_t offset){
  HANDLE h = handle_of(fd);
  while(!buf.empty()){
    DWORD want = static_cast<DWORD>(std::min(buf.size(), MAX_TRANSFER)), put = 0;
    OVERLAPPED ov = at_offset(offset);
    if(!WriteFile(h, buf.data(), want, &put, &ov) || put == 0){
      errno = EIO;
      return false;
    }
    buf = buf.subspan(put);
    offset += put;
  }
  return true;
}

uint64_t file_size(int fd){
  LARGE_INTEGER size;
  return GetFileSizeEx(handle_of(fd), &size) ? static_cast<uint64_t>(size.QuadPart) : 0;
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
  return _chsize_s(fd, static_cast<long long>(size)) == 0;
}

bool fs_same_file(int fd_a, int fd_b){
  BY_HANDLE_FILE_INFORMATION a, b;
  return GetFileInformationByHandle(handle_of(fd_a), &a) && GetFileInformationByHandle(handle_of(fd_b), &b) &&
         a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
         a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

std::optional<std::string> fs_absolute_path(const std::string &path){
  /* With a null buffer, _fullpath() allocates the result with malloc(). */
  c_string full(_fullpath(nullptr, path.c_str(), 0));
  if(!full || GetFileAttributesA(full.get()) == INVALID_FILE_ATTRIBUTES){
    errno = ENOENT;
    return std::nullopt;
  }
  /* Drop trailing separators, except the one of a drive root such as "C:\". */
  std::string out(full.get());
  while(out.size() > 1 && fs_is_separator(out.back()) && !fs_is_root(out)) out.pop_back();
  return out;
}

std::optional<std::string> fs_current_dir(){
  DWORD size = GetCurrentDirectoryA(0, nullptr);
  if(size == 0) return std::nullopt;
  std::string dir(size, '\0');
  DWORD len = GetCurrentDirectoryA(size, dir.data());
  if(len == 0 || len >= size) return std::nullopt;
  dir.resize(len);
  return dir;
}

bool fs_exists(const std::string &path){
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/**
 * @brief  Opens a file for reading and writing, optionally locking it.
 * @param  path Path of the file.
 * @param  lock True to acquire the exclusive lock.
 * @return The descriptor, empty on error.
 */
static unique_fd reopen(const std::string &path, bool lock){
  unique_fd fd(open(path.c_str(), O_RDWR | O_BINARY));
  if(fd && lock) fs_lock(fd.get());
  return fd;
}

bool fs_replace_file(const std::string &from, unique_fd &from_fd, const std::string &to, unique_fd &to_fd){
  from_fd.reset();
  to_fd.reset();
  bool moved = MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

  if(!moved){
    errno = EACCES;
    to_fd = reopen(to, true);
    from_fd = reopen(from, false);
    return false;
  }
  to_fd = reopen(to, true);
  return static_cast<bool>(to_fd);
}

unique_fd fs_open_regular(const std::string &path, bool write){
  /* FILE_SHARE_DELETE lets the GUI compact (replace) the file while it is open here. */
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | (write ? GENERIC_WRITE : 0),
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if(h == INVALID_HANDLE_VALUE) return unique_fd();
  BY_HANDLE_FILE_INFORMATION info;
  if(!GetFileInformationByHandle(h, &info) || GetFileType(h) != FILE_TYPE_DISK ||
     (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))){
    CloseHandle(h);
    return unique_fd();
  }
  int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), (write ? _O_RDWR : _O_RDONLY) | _O_BINARY);
  if(fd < 0) CloseHandle(h);
  return unique_fd(fd);
}

/**
 * @brief  Formats a number in hexadecimal.
 * @param  value     Number.
 * @param  min_width Minimum number of digits, padded with zeros.
 * @return Lowercase digits, without a prefix.
 */
static std::string to_hex(uint64_t value, std::size_t min_width = 0){
  std::array<char, 16> digits;
  auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
  std::string text(digits.data(), end);
  if(text.size() < min_width) text.insert(0, min_width - text.size(), '0');
  return text;
}

std::optional<std::string> fs_file_id(int fd){
  BY_HANDLE_FILE_INFORMATION info;
  if(!GetFileInformationByHandle(handle_of(fd), &info)) return std::nullopt;
  return to_hex(info.dwVolumeSerialNumber) + "-" + to_hex(info.nFileIndexHigh) + to_hex(info.nFileIndexLow, 8);
}

unique_fd fs_temp_file(){
  std::array<char, MAX_PATH> dir, path;
  if(!GetTempPathA(static_cast<DWORD>(dir.size()), dir.data()) || !GetTempFileNameA(dir.data(), "crl", 0, path.data()))
    return unique_fd();
  /* _O_TEMPORARY deletes the file when the last descriptor is closed. */
  return unique_fd(open(path.data(), O_RDWR | O_TRUNC | O_BINARY | _O_TEMPORARY));
}

bool fs_is_separator(char c){
  return c == '/' || c == '\\';
}

/**
 * @brief  Checks whether c is an ASCII letter, as in a drive name.
 * @param  c Character.
 * @return True for 'A' to 'Z' and 'a' to 'z'.
 */
static bool is_drive_letter(char c){
  return ascii_lower(c) >= 'a' && ascii_lower(c) <= 'z';
}

bool fs_is_absolute(std::string_view path){
  /* "\\x" and "/x" are relative to the current drive, which is good enough here. */
  if(!path.empty() && fs_is_separator(path[0])) return true;
  return path.size() >= 3 && is_drive_letter(path[0]) && path[1] == ':' && fs_is_separator(path[2]);
}

bool fs_is_root(std::string_view path){
  if(path.size() == 1) return fs_is_separator(path[0]);
  return path.size() == 3 && path[1] == ':' && fs_is_separator(path[2]);
}

std::string_view fs_basename(std::string_view path){
  std::size_t last = path.find_last_of("/\\:");
  return last == std::string_view::npos ? path : path.substr(last + 1);
}

/**
 * @brief  Checks whether name is a reserved device name such as "CON", "NUL"
 *         or "COM1", with or without an extension ("nul.txt" is the device
 *         too).
 * @param  name File name.
 * @return True if the name refers to a device.
 */
static bool is_device_name(std::string_view name){
  static constexpr std::array<std::string_view, 6> devices = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
  std::string_view stem = name.substr(0, name.find('.'));
  if(std::ranges::any_of(devices, [&](std::string_view d){ return equals_nocase(stem, d); })) return true;
  return stem.size() == 4 && (starts_with_nocase(stem, "COM") || starts_with_nocase(stem, "LPT")) &&
         stem[3] >= '1' && stem[3] <= '9';
}

bool fs_is_plain_name(std::string_view name){
  if(name.empty() || name == "." || name == "..") return false;
  /* Windows silently strips trailing dots and spaces, so "..." would mean "..". */
  if(name.back() == '.' || name.back() == ' ') return false;
  constexpr std::string_view forbidden = "/\\:*?\"<>|";
  bool bad_char = std::ranges::any_of(name, [&](char c){
    return static_cast<unsigned char>(c) < 32 || forbidden.find(c) != std::string_view::npos;
  });
  return !bad_char && !is_device_name(name);
}

}
