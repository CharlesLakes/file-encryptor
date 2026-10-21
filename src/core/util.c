/**
 * @file    util.c
 * @brief   Implementation of the general-purpose helpers (see util.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

void join_path(char *out, size_t out_size, const char *dir, const char *name){
  if(strcmp(dir, "/") == 0) snprintf(out, out_size, "/%s", name);
  else snprintf(out, out_size, "%s/%s", dir, name);
}
