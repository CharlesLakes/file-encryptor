/**
 * @file    util.cpp
 * @brief   Implementation of the general-purpose helpers (see util.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/util.h"

#include <stdio.h>
#include <string.h>
#include "core/fs.h"

namespace crls{

void put_le(unsigned char *p, uint64_t value, int bytes){
  for(int i = 0; i < bytes; i++)
    p[i] = (unsigned char) (value >> (8*i));
}

uint64_t get_le(const unsigned char *p, int bytes){
  uint64_t value = 0;
  for(int i = 0; i < bytes; i++)
    value |= (uint64_t) p[i] << (8*i);
  return value;
}

void format_size(uint64_t size, char *out, size_t out_size){
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = (double) size;
  int unit = 0;
  while(value >= 1024 && unit < 4){
    value /= 1024;
    unit++;
  }
  if(unit == 0) snprintf(out, out_size, "%llu B", (unsigned long long) size);
  else snprintf(out, out_size, "%.1f %s", value, units[unit]);
}

void join_path(char *out, size_t out_size, const char *dir, const char *name){
  size_t len = strlen(dir);
  if(len > 0 && fs_is_separator(dir[len - 1])) snprintf(out, out_size, "%s%s", dir, name);
  else snprintf(out, out_size, "%s/%s", dir, name);
}

}
