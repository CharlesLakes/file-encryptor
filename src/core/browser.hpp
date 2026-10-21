/**
 * @file    browser.hpp
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

#ifndef CORE_BROWSER_HPP
#define CORE_BROWSER_HPP

#include <string>
#include <vector>

namespace crls{

/**
 * @brief Single entry of a directory listing.
 */
struct dir_item{
  std::string name;          /**< Entry name. */
  bool is_dir = false;       /**< True if the entry is a directory. */
};

/**
 * @brief Which entries besides folders are listed.
 */
enum class browser_filter{
  crls,                      /**< Regular files with the .crls extension. */
  all,                       /**< All regular files. */
  dirs                       /**< Directories only. */
};

/**
 * @brief Listing of one folder, filtered and sorted, with a selection.
 */
class browser{
public:
  /** @brief Moves to the current working directory and lists it. */
  void open_cwd();

  /** @brief Lists dir() again with the current filter and resets the selection and scroll. */
  void load();

  /**
   * @brief Changes into a subfolder (or ".."), relative to dir(), and lists it.
   * @param name Name of the subfolder.
   */
  void enter(const std::string &name);

  /**
   * @brief Sets the listed folder without listing it (call load() next).
   * @param dir Absolute path of a folder.
   */
  void set_dir(std::string dir){ dir_ = std::move(dir); }

  /** @brief Absolute path of the listed folder. */
  const std::string& dir() const noexcept{ return dir_; }

  /** @brief Entries of the folder, sorted. */
  const std::vector<dir_item>& items() const noexcept{ return items_; }

  browser_filter filter = browser_filter::crls;  /**< Entries listed besides folders. */
  int sel = -1;                                   /**< Selected row, or -1 if none. */
  int scroll = 0;                                 /**< First visible row. */

private:
  std::string dir_ = "/";
  std::vector<dir_item> items_;
};

}

#endif
