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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"

namespace crls{

/**
 * @brief  Order of the listing: ".." first, then folders, then files, each
 *         group in case-insensitive order.
 * @param  x First item.
 * @param  y Second item.
 * @return True if x goes before y.
 */
static bool item_before(const dir_item &x, const dir_item &y){
  bool x_up = strcmp(x.name, "..") == 0, y_up = strcmp(y.name, "..") == 0;
  if(x_up || y_up) return x_up && !y_up;
  if(x.is_dir != y.is_dir) return x.is_dir;
  return strcasecmp(x.name, y.name) < 0;
}

void browser_init(browser *b){
  b->items.clear();
  b->filter = FILTER_CRLS;
  b->sel = -1;
  b->scroll = 0;
  if(!getcwd(b->dir, sizeof(b->dir))) strcpy(b->dir, "/");
  browser_load(b);
}

void browser_free(browser *b){
  std::vector<dir_item>().swap(b->items);
}

void browser_load(browser *b){
  b->items.clear();
  b->sel = -1;
  b->scroll = 0;

  DIR *dir = opendir(b->dir);
  if(!dir){
    set_status(true, "Could not open the folder %s: %s", b->dir, strerror(errno));
    return;
  }

  if(!fs_is_root(b->dir)){
    dir_item up = {"..", true};
    b->items.push_back(up);
  }

  struct dirent *ent;
  while((ent = readdir(dir))){
    if(ent->d_name[0] == '.' || strlen(ent->d_name) > MAX_NAME) continue;

    char path[PATH_MAX];
    struct stat info;
    join_path(path, sizeof(path), b->dir, ent->d_name);
    if(stat(path, &info) != 0) continue;

    dir_item item;
    strcpy(item.name, ent->d_name);
    item.is_dir = S_ISDIR(info.st_mode);

    bool keep = item.is_dir;
    if(b->filter == FILTER_CRLS) keep = keep || (S_ISREG(info.st_mode) && has_crls_ext(item.name));
    if(b->filter == FILTER_ALL) keep = keep || S_ISREG(info.st_mode);
    if(keep) b->items.push_back(item);
  }
  closedir(dir);

  std::sort(b->items.begin(), b->items.end(), item_before);
  if(!b->items.empty()) b->sel = 0;
}

void browser_enter(browser *b, const char *name){
  char candidate[PATH_MAX], resolved[PATH_MAX];
  join_path(candidate, sizeof(candidate), b->dir, name);
  if(!fs_absolute_path(candidate, resolved)){
    set_status(true, "Could not enter %s: %s", candidate, strerror(errno));
    return;
  }
  strcpy(b->dir, resolved);
  browser_load(b);
}

dir_item* browser_get_item(browser *b, unsigned int index){
  return &b->items[index];
}

}
