/**
 * @file    test_util.cpp
 * @brief   Implementation of the test helpers (see test_util.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "test_util.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sys/stat.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

using namespace crls;

namespace{

int failures = 0;                  /* Failed checks so far. */
const temp_dir *current = nullptr; /* Folder used by path_in(). */

/** @brief Deleter of a FILE. */
struct file_closer{
  void operator()(FILE *f) const noexcept{ fclose(f); }
};

using unique_file = std::unique_ptr<FILE, file_closer>;

}

void fail(std::string_view message, std::source_location where){
  printf("FAIL %s:%u: %.*s (status: %s)\n", where.file_name(), static_cast<unsigned>(where.line()),
         static_cast<int>(message.size()), message.data(), status_message());
  failures++;
}

int failure_count(){
  return failures;
}

void forget_failures(int count){
  failures = count;
}

void check_true(bool ok, std::string_view text, std::source_location where){
  if(!ok) fail(text, where);
}

bool make_dir(const std::string &path){
#ifdef _WIN32
  return _mkdir(path.c_str()) == 0;
#else
  return mkdir(path.c_str(), 0700) == 0;
#endif
}

temp_dir::temp_dir(){
#ifdef _WIN32
  const char *base = getenv("TEMP");
  std::string pattern = std::string(base ? base : ".") + "\\file-encryptor-test-XXXXXX";
  if(_mktemp(pattern.data()) && make_dir(pattern)) path_ = pattern;
#else
  std::string pattern = "/tmp/file-encryptor-test-XXXXXX";
  if(mkdtemp(pattern.data())) path_ = pattern;
#endif
  current = this;
}

temp_dir::~temp_dir(){
  if(current == this) current = nullptr;
  if(path_.empty()) return;
#ifdef _WIN32
  std::string command = "rmdir /s /q \"" + path_ + "\"";
#else
  std::string command = "rm -rf '" + path_ + "'";
#endif
  if(system(command.c_str()) != 0) printf("could not remove %s\n", path_.c_str());
}

std::string path_in(std::string_view name){
  return join_path(current ? current->path() : std::string("."), name);
}

void make_file(std::string_view name, std::size_t size, unsigned seed){
  std::string data(size, '\0');
  uint32_t x = seed * 2654435761u + 1;
  for(char &c : data){
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c = static_cast<char>(x & 0xff);
  }
  if(!write_file(path_in(name), data)) fail("could not create " + std::string(name));
}

std::optional<std::string> read_file(const std::string &path){
  unique_file f(fopen(path.c_str(), "rb"));
  if(!f) return std::nullopt;
  std::string data;
  std::array<char, 4096> chunk;
  while(std::size_t n = fread(chunk.data(), 1, chunk.size(), f.get())) data.append(chunk.data(), n);
  if(ferror(f.get())) return std::nullopt;
  return data;
}

bool write_file(const std::string &path, std::string_view data){
  unique_file f(fopen(path.c_str(), "wb"));
  if(!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f.get()) == data.size();
  return fclose(f.release()) == 0 && ok;
}

bool copy_file(const std::string &from, const std::string &to){
  std::optional<std::string> data = read_file(from);
  return data && write_file(to, *data);
}

bool same_files(const std::string &a, const std::string &b){
  std::optional<std::string> x = read_file(a), y = read_file(b);
  return x && y && *x == *y;
}

uint64_t size_of(const std::string &path){
  unique_fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
  return fd ? file_size(fd.get()) : 0;
}

bool status_mentions(std::string_view text){
  return std::string_view(status_message()).find(text) != std::string_view::npos;
}

void check_extract(archive &a, std::string_view out_name, std::span<const std::string_view> names){
  std::string out = path_in(out_name);
  CHECK(make_dir(out));
  if(!names.empty()) CHECK_EQ(a.entries().size(), names.size());
  for(std::size_t i = 0; i < a.entries().size(); i++){
    const std::string &name = a.entries()[i].name;
    if(i < names.size()) CHECK_EQ(name, names[i]);
    CHECK(a.extract(i, out) == extract_result::extracted);
    CHECK(a.extract(i, out) == extract_result::skipped);
    CHECK(same_files(path_in(name), join_path(out, name)));
  }
}

bool prepare_upload(const std::string &crls, const char *password, std::string_view file, append_upload &up){
  archive a;
  return a.open(crls, password) && up.prepare(a, path_in(file));
}

int test_summary(){
  if(failures) printf("%d check(s) failed\n", failures);
  else printf("All tests passed\n");
  return failures ? 1 : 0;
}
