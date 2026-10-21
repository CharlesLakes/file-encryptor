/**
 * @file    browser.cpp
 * @brief   Implementation of the directory browser (see browser.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/browser.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include "core/archive.h"
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"

namespace crls{

namespace{

/**
 * @brief  Order of the listing: ".." first, then folders, then files, each
 *         group in case-insensitive order.
 * @param  x First item.
 * @param  y Second item.
 * @return True if x goes before y.
 */
bool item_before(const dir_item &x, const dir_item &y){
  bool x_up = x.name == "..", y_up = y.name == "..";
  if(x_up || y_up) return x_up && !y_up;
  if(x.is_dir != y.is_dir) return x.is_dir;
  return less_nocase(x.name, y.name);
}

/** @brief Deleter of a DIR. */
struct dir_closer{
  void operator()(DIR *d) const noexcept{ closedir(d); }
};

/** @brief A DIR that is closed when it goes out of scope. */
using unique_dir = std::unique_ptr<DIR, dir_closer>;

}

void browser::open_cwd(){
  dir_ = fs_current_dir().value_or("/");
  load();
}

void browser::load(){
  items_.clear();
  sel = -1;
  scroll = 0;

  unique_dir dir(opendir(dir_.c_str()));
  if(!dir){
    set_status(true, "Could not open the folder %s: %s", dir_.c_str(), strerror(errno));
    return;
  }

  if(!fs_is_root(dir_)) items_.push_back({"..", true});

  while(const dirent *ent = readdir(dir.get())){
    std::string_view name = ent->d_name;
    if(name.starts_with('.') || name.size() > MAX_NAME) continue;

    struct stat info;
    if(stat(join_path(dir_, name).c_str(), &info) != 0) continue;

    dir_item item{std::string(name), S_ISDIR(info.st_mode)};
    bool keep = item.is_dir;
    if(filter == browser_filter::crls) keep = keep || (S_ISREG(info.st_mode) && has_crls_ext(item.name));
    if(filter == browser_filter::all) keep = keep || S_ISREG(info.st_mode);
    if(keep) items_.push_back(std::move(item));
  }

  std::ranges::sort(items_, item_before);
  if(!items_.empty()) sel = 0;
}

void browser::enter(const std::string &name){
  std::string candidate = join_path(dir_, name);
  std::optional<std::string> absolute = fs_absolute_path(candidate);
  if(!absolute){
    set_status(true, "Could not enter %s: %s", candidate.c_str(), strerror(errno));
    return;
  }
  dir_ = std::move(*absolute);
  load();
}

}
