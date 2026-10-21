/**
 * @file    browser.h
 * @brief   Directory browser: filtered and sorted listing of a single folder.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section browser_overview Overview
 *
 * Used by the user interface to select a .crls file, a file to add or a
 * destination folder for extraction. The parent entry ".." is always listed
 * first, followed by folders and then files, each group sorted
 * case-insensitively. Hidden entries (names starting with '.') are omitted.
 */

#ifndef CORE_BROWSER_H
#define CORE_BROWSER_H

#include <stdbool.h>
#include <limits.h>
#include "core/archive.h"
#include <vector>

/**
 * @brief Single entry of a directory listing.
 */
typedef struct{
  char name[MAX_NAME + 1];   /**< Entry name. */
  bool is_dir;               /**< True if the entry is a directory. */
}dir_item;

/**
 * @brief Listing filters. Directories are always included.
 */
enum{
  FILTER_CRLS,               /**< Regular files with the .crls extension. */
  FILTER_ALL,                /**< All regular files. */
  FILTER_DIRS                /**< Directories only. */
};

/**
 * @brief State of the directory browser.
 */
typedef struct{
  char dir[PATH_MAX];        /**< Absolute path of the listed folder. */
  std::vector<dir_item> items;   /**< Entries of the folder, sorted. */
  int filter;                /**< One of the FILTER_* values. */
  int sel;                   /**< Selected row, or -1 if none. */
  int scroll;                /**< First visible row. */
}browser;

/**
 * @brief Initializes the browser on the current working directory.
 * @param b Browser.
 */
void      browser_init(browser *b);

/**
 * @brief Releases the memory owned by the browser.
 * @param b Browser.
 */
void      browser_free(browser *b);

/**
 * @brief Reloads b->dir using b->filter and resets the selection and scroll.
 * @param b Browser.
 */
void      browser_load(browser *b);

/**
 * @brief Changes into the subfolder name (or ".."), relative to b->dir.
 * @param b    Browser.
 * @param name Name of the subfolder.
 */
void      browser_enter(browser *b, const char *name);

/**
 * @brief  Returns the item at index. No bounds checking is performed.
 * @param  b     Browser.
 * @param  index Position of the item, 0 <= index < b->items.size().
 * @return Pointer to the item.
 */
dir_item* browser_get_item(browser *b, unsigned int index);

#endif
