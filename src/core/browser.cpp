/**
 * @file    browser.cpp
 * @brief   Implementation of the directory browser (see browser.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/browser.h"

#include <algorithm>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <unistd.h>
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
  return strcasecmp(x.name.c_str(), y.name.c_str()) < 0;
}

}

void browser::open_cwd(){
  char cwd[PATH_MAX];
  dir_ = getcwd(cwd, sizeof(cwd)) ? cwd : "/";
  load();
}

void browser::load(){
  items_.clear();
  sel = -1;
  scroll = 0;

  DIR *dir = opendir(dir_.c_str());
  if(!dir){
    set_status(true, "Could not open the folder %s: %s", dir_.c_str(), strerror(errno));
    return;
  }

  if(!fs_is_root(dir_.c_str())) items_.push_back({"..", true});

  struct dirent *ent;
  while((ent = readdir(dir))){
    if(ent->d_name[0] == '.' || strlen(ent->d_name) > MAX_NAME) continue;

    char path[PATH_MAX];
    struct stat info;
    join_path(path, sizeof(path), dir_.c_str(), ent->d_name);
    if(stat(path, &info) != 0) continue;

    dir_item item{ent->d_name, S_ISDIR(info.st_mode)};
    bool keep = item.is_dir;
    if(filter == browser_filter::crls) keep = keep || (S_ISREG(info.st_mode) && has_crls_ext(item.name));
    if(filter == browser_filter::all) keep = keep || S_ISREG(info.st_mode);
    if(keep) items_.push_back(std::move(item));
  }
  closedir(dir);

  std::sort(items_.begin(), items_.end(), item_before);
  if(!items_.empty()) sel = 0;
}

void browser::enter(const std::string &name){
  char candidate[PATH_MAX];
  join_path(candidate, sizeof(candidate), dir_.c_str(), name.c_str());
  std::string absolute;
  if(!fs_absolute_path(candidate, absolute)){
    set_status(true, "Could not enter %s: %s", candidate, strerror(errno));
    return;
  }
  dir_ = std::move(absolute);
  load();
}

}
