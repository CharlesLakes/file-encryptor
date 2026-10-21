/**
 * @file    test_util.h
 * @brief   Helpers shared by the tests: checks, a temporary folder and files
 *          with known contents.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#ifndef TESTS_TEST_UTIL_H
#define TESTS_TEST_UTIL_H

#include <climits>
#include <cstddef>
#include <cstdio>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "core/status.h"

extern int failures;           /**< Number of failed checks so far. */
extern char test_dir[PATH_MAX];/**< Temporary folder of the running test. */

/** @brief Records a failure, with the current status message, if cond is false. */
#define CHECK(cond) do{ \
    if(!(cond)){ printf("FAIL %s:%d: %s (status: %s)\n", __FILE__, __LINE__, #cond, crls::status_text); failures++; } \
  }while(0)

/** @brief Records a failure that shows both values if a is not equal to b. */
#define CHECK_EQ(a, b) check_eq((a), (b), #a, #b)

/**
 * @brief  Describes a value for a failure message.
 * @param  v Value: a number, an enum or something string-like.
 * @return Its text.
 */
template<class T>
std::string describe(const T &v){
  if constexpr(std::is_enum_v<T>) return "enum " + std::to_string(static_cast<long long>(v));
  else if constexpr(std::is_arithmetic_v<T>) return std::to_string(v);
  else return "\"" + std::string(std::string_view(v)) + "\"";
}

/**
 * @brief Implementation of CHECK_EQ(): compares integers of any signedness by
 *        value (std::cmp_equal) and everything else with ==.
 */
template<class A, class B>
void check_eq(const A &a, const B &b, const char *a_text, const char *b_text,
              std::source_location where = std::source_location::current()){
  bool equal;
  if constexpr(std::is_integral_v<A> && std::is_integral_v<B> && !std::is_same_v<A, bool> && !std::is_same_v<B, bool>)
    equal = std::cmp_equal(a, b);
  else
    equal = a == b;
  if(equal) return;
  printf("FAIL %s:%u: %s == %s (%s vs %s; status: %s)\n", where.file_name(), static_cast<unsigned>(where.line()),
         a_text, b_text, describe(a).c_str(), describe(b).c_str(), crls::status_text);
  failures++;
}

/**
 * @brief A uniquely named temporary folder, kept in test_dir and removed with
 *        everything in it when the object is destroyed.
 */
class temp_dir{
public:
  temp_dir();
  ~temp_dir();

  temp_dir(const temp_dir &) = delete;
  temp_dir& operator=(const temp_dir &) = delete;

  /** @brief True if the folder was created. */
  explicit operator bool() const noexcept{ return created_; }

private:
  bool created_ = false;
};

/**
 * @brief  Creates a folder.
 * @param  path Path of the folder.
 * @return 0 on success, -1 on error.
 */
int  make_dir(const char *path);

/**
 * @brief Builds the path of name inside test_dir.
 * @param out  Destination buffer of PATH_MAX bytes.
 * @param name File name.
 */
void path_in(char *out, const char *name);

/**
 * @brief Writes size pseudo-random bytes to name inside test_dir.
 *
 * Uses its own xorshift generator instead of rand(), so the contents are the
 * same on every platform; tests/fixtures/v2.crls relies on it.
 *
 * @param name File name.
 * @param size Number of bytes.
 * @param seed Seed of the pseudo-random generator.
 */
void make_file(const char *name, size_t size, unsigned seed);

/**
 * @brief  Copies a file.
 * @param  from Source path.
 * @param  to   Destination path.
 * @return True on success, false otherwise.
 */
bool copy_file(const char *from, const char *to);

/**
 * @brief  Compares the contents of two files byte by byte.
 * @param  a Path of the first file.
 * @param  b Path of the second file.
 * @return True if both files exist and are identical.
 */
bool same_files(const char *a, const char *b);

/**
 * @brief  Prints the summary line of a test program.
 * @return Exit code: 0 if every check passed, 1 otherwise.
 */
int  test_summary();

#endif
