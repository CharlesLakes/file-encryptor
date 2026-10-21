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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "core/append.h"
#include "core/archive.h"

/**
 * @brief Records a failed check: prints where it happened, the message and the
 *        current status message.
 * @param message What failed.
 * @param where   Location of the check (filled in by the compiler).
 */
void fail(std::string_view message, std::source_location where = std::source_location::current());

/** @brief Number of failed checks so far. */
int  failure_count();

/**
 * @brief Forgets the failures recorded after the first count, for a test that
 *        makes a check fail on purpose.
 * @param count Number of failures to keep.
 */
void forget_failures(int count);

/**
 * @brief Implementation of CHECK(): records a failure if ok is false.
 * @param ok    Result of the check.
 * @param text  The checked expression, as written.
 * @param where Location of the check.
 */
void check_true(bool ok, std::string_view text, std::source_location where = std::source_location::current());

/* Macros only to turn the expressions into text; the work is done by functions. */

/** @brief Records a failure, with the current status message, if cond is false. */
#define CHECK(cond) check_true(static_cast<bool>(cond), #cond)

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
void check_eq(const A &a, const B &b, std::string_view a_text, std::string_view b_text,
              std::source_location where = std::source_location::current()){
  bool equal;
  if constexpr(std::is_integral_v<A> && std::is_integral_v<B> && !std::is_same_v<A, bool> && !std::is_same_v<B, bool>)
    equal = std::cmp_equal(a, b);
  else
    equal = a == b;
  if(!equal)
    fail(std::string(a_text) + " == " + std::string(b_text) + " (" + describe(a) + " vs " + describe(b) + ")", where);
}

/**
 * @brief A uniquely named temporary folder, removed with everything in it
 *        when the object is destroyed. While it exists, path_in() and
 *        make_file() work inside it.
 */
class temp_dir{
public:
  temp_dir();
  ~temp_dir();

  temp_dir(const temp_dir &) = delete;
  temp_dir& operator=(const temp_dir &) = delete;

  /** @brief True if the folder was created. */
  explicit operator bool() const noexcept{ return !path_.empty(); }

  /** @brief Path of the folder (empty if it could not be created). */
  const std::string& path() const noexcept{ return path_; }

private:
  std::string path_;
};

/**
 * @brief  Creates a folder.
 * @param  path Path of the folder.
 * @return True on success.
 */
bool make_dir(const std::string &path);

/**
 * @brief  Builds the path of name inside the current temp_dir.
 * @param  name File name (may contain '/').
 * @return The path.
 */
std::string path_in(std::string_view name);

/**
 * @brief Writes size pseudo-random bytes to name inside the current temp_dir.
 *
 * Uses its own xorshift generator instead of rand(), so the contents are the
 * same on every platform; tests/fixtures/v2.crls relies on it.
 *
 * @param name File name.
 * @param size Number of bytes.
 * @param seed Seed of the pseudo-random generator.
 */
void make_file(std::string_view name, std::size_t size, unsigned seed);

/**
 * @brief  Reads a whole file.
 * @param  path Path of the file.
 * @return Its bytes, or std::nullopt if it cannot be read.
 */
std::optional<std::string> read_file(const std::string &path);

/**
 * @brief  Writes a whole file, replacing it.
 * @param  path Path of the file.
 * @param  data Bytes to write.
 * @return True on success.
 */
bool write_file(const std::string &path, std::string_view data);

/**
 * @brief  Copies a file.
 * @param  from Source path.
 * @param  to   Destination path.
 * @return True on success, false otherwise.
 */
bool copy_file(const std::string &from, const std::string &to);

/**
 * @brief  Compares the contents of two files.
 * @param  a Path of the first file.
 * @param  b Path of the second file.
 * @return True if both files exist and are identical.
 */
bool same_files(const std::string &a, const std::string &b);

/**
 * @brief  Returns the size of a file.
 * @param  path Path of the file.
 * @return Size in bytes, or 0 if it cannot be opened.
 */
uint64_t size_of(const std::string &path);

/**
 * @brief  Checks whether the status message contains some text.
 * @param  text Text to look for.
 * @return True if it is there.
 */
bool status_mentions(std::string_view text);

/**
 * @brief Extracts every entry of an open archive into a new folder, checks that
 *        extracting again skips the existing file, and compares each file with
 *        the original of the same name in the test folder.
 * @param a        Open archive.
 * @param out_name Name of the new folder inside the test folder.
 * @param names    Expected entry names in order, or empty to accept any.
 */
void check_extract(crls::archive &a, std::string_view out_name, std::span<const std::string_view> names = {});

/**
 * @brief  Opens an archive, prepares the upload of a file and closes the
 *         archive again, as a client does before sending the request.
 * @param  crls     Path of the archive.
 * @param  password Password used to open it.
 * @param  file     Name of the file to upload, inside the test folder.
 * @param  up       Output: the prepared upload.
 * @return True on success, false otherwise.
 */
bool prepare_upload(const std::string &crls, const char *password, std::string_view file, crls::append_upload &up);

/**
 * @brief  Prints the summary line of a test program.
 * @return Exit code: 0 if every check passed, 1 otherwise.
 */
int  test_summary();

#endif
