/**
 * @file    test_lib.cpp
 * @brief   Tests of the basic C++ types in src/lib/.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <fcntl.h>
#include "core/archive.hpp"
#include "core/fs.hpp"
#include "lib/function_ref.hpp"
#include "lib/heap_buffer.hpp"
#include "lib/secret.hpp"
#include "lib/unique_fd.hpp"
#include "test_util.hpp"

using namespace crls;

/**
 * @brief  Calls f with every value from 1 to n and returns the sum of the
 *         results, as a callback-taking function would.
 * @param  n Number of calls.
 * @param  f Callback.
 * @return Sum of the results.
 */
static int sum_calls(int n, crls::function_ref<int(int)> f){
  int total = 0;
  for(int i = 1; i <= n; i++) total += f(i);
  return total;
}

/**
 * @brief Checks that the destructor of a secret wipes its bytes, by building
 *        one inside a buffer of ours and looking at the buffer afterwards.
 */
static void check_secret_wipe(){
  alignas(crls::secret<32>) unsigned char storage[sizeof(crls::secret<32>)];
  auto *s = new (storage) crls::secret<32>();
  std::ranges::fill(s->span(), 0xAB);
  CHECK_EQ(s->data()[31], 0xAB);
  s->~basic_secret();
  CHECK(std::ranges::all_of(storage, [](unsigned char c){ return c == 0; }));
}

int main(){
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  /* unique_fd: ownership, move and release. */
  std::string path = path_in("file.bin");
  make_file("file.bin", 100, 1);
  {
    crls::unique_fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
    CHECK(static_cast<bool>(fd));
    CHECK_EQ(file_size(fd.get()), 100);
    crls::unique_fd moved(std::move(fd));
    CHECK(!fd && moved);
    int raw = moved.release();
    CHECK(!moved && raw >= 0);
    crls::unique_fd again(raw);
    again.reset();
    CHECK(!again);
  }
  crls::unique_fd empty;
  CHECK(!empty && empty.get() == -1);

  /* secret: zero-initialized, wiped on request and on destruction. */
  crls::secret_text<16> password;
  CHECK(password.data()[0] == '\0' && password.size() == 16);
  std::ranges::copy(std::string_view("hunter2"), password.data());
  password.wipe();
  CHECK(password.data()[0] == '\0' && password.data()[6] == '\0');
  check_secret_wipe();

  /* secret_string: grows and shrinks in place, up to its capacity. */
  crls::secret_string<4> typed;
  CHECK(typed.empty() && typed.c_str()[0] == '\0');
  for(char c : std::string_view("abcde")) typed.push_back(c);
  CHECK(typed.view() == "abcd" && typed.size() == 4 && typed.c_str()[4] == '\0');
  typed.pop_back();
  CHECK(typed.view() == "abc" && typed.c_str()[3] == '\0');
  typed.wipe();
  CHECK(typed.empty() && typed.c_str()[0] == '\0');

  /* heap_buffer: zeroed contents, move, and failure without aborting. */
  crls::heap_buffer buf;
  CHECK(buf.allocate(1000) && buf.size() == 1000 && buf.data()[999] == 0);
  buf.data()[0] = 7;
  crls::heap_buffer other(std::move(buf));
  CHECK(buf.size() == 0 && buf.data() == nullptr && other.size() == 1000 && other.data()[0] == 7);
  CHECK(other.allocate(0) && other.size() == 0 && other.data() != nullptr);
  CHECK(!other.allocate(std::numeric_limits<std::size_t>::max() / 2) && other.size() == 0);
  crls::secret_buffer key;
  CHECK(key.allocate(32) && key.span().size() == 32);
  crls::secret_buffer moved;
  moved = std::move(key);
  CHECK(key.size() == 0 && moved.size() == 32);
  /* A secret can only move into another buffer that wipes it. */
  static_assert(!std::is_assignable_v<crls::heap_buffer &, crls::secret_buffer &&>);
  static_assert(!std::is_constructible_v<crls::heap_buffer, crls::secret_buffer &&>);

  /* function_ref: calls the lambda it refers to, which keeps its state. */
  int calls = 0;
  auto square = [&](int x){
    calls++;
    return x * x;
  };
  CHECK(sum_calls(3, square) == 1 + 4 + 9 && calls == 3);
  crls::function_ref<int(int)> none;
  CHECK(!none);

  /* CHECK_EQ reports a mismatch, also between integers of different signedness;
     the expected failure is undone here. */
  int before = failure_count();
  printf("The next FAIL line is expected:\n");
  CHECK_EQ(-1, static_cast<unsigned>(-1));
  CHECK_EQ(std::string("same"), "same");
  CHECK_EQ(sum_calls(2, square), 5u);
  bool reported = failure_count() == before + 1;
  forget_failures(before);
  CHECK(reported);

  return test_summary();
}
