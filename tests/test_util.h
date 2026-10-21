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

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "core/status.h"

extern int failures;           /**< Number of failed checks so far. */
extern char test_dir[PATH_MAX];/**< Temporary folder of the running test. */

/** @brief Records a failure, with the current status message, if cond is false. */
#define CHECK(cond) do{ \
    if(!(cond)){ printf("FAIL %s:%d: %s (status: %s)\n", __FILE__, __LINE__, #cond, status_text); failures++; } \
  }while(0)

/**
 * @brief  Creates a new, uniquely named folder for the test files in test_dir.
 * @return True on success, false otherwise.
 */
bool make_temp_dir(void);

/**
 * @brief Removes test_dir and everything in it.
 */
void remove_temp_dir(void);

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
int  test_summary(void);

#endif
