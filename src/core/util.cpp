/**
 * @file    util.cpp
 * @brief   Implementation of the general-purpose helpers (see util.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/util.h"

#include <array>
#include <charconv>
#include "core/fs.h"

namespace crls{

std::string format_size(uint64_t size){
  static constexpr std::array<std::string_view, 5> units = {"B", "KB", "MB", "GB", "TB"};
  if(size < 1024) return std::to_string(size) + " B";
  double value = static_cast<double>(size);
  std::size_t unit = 0;
  while(value >= 1024 && unit < units.size() - 1){
    value /= 1024;
    unit++;
  }
  std::array<char, 32> digits;
  auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), value, std::chars_format::fixed, 1);
  std::string text(digits.data(), ec == std::errc() ? end : digits.data());
  text += ' ';
  text += units[unit];
  return text;
}

std::string join_path(std::string_view dir, std::string_view name){
  std::string path(dir);
  if(path.empty() || !fs_is_separator(path.back())) path += '/';
  path += name;
  return path;
}

}
