/**
 * @file    browser.c
 * @brief   Implementation of the directory browser (see browser.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/browser.h"

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

/**
 * @brief  qsort() comparator: ".." first, then folders, then files, each
 *         group in case-insensitive order.
 * @param  a First dir_item.
 * @param  b Second dir_item.
 * @return Negative, zero or positive, as required by qsort().
 */
static int compare_items(const void *a, const void *b){
  const dir_item *x = (const dir_item *) a, *y = (const dir_item *) b;
  if(strcmp(x->name, "..") == 0) return -1;
  if(strcmp(y->name, "..") == 0) return 1;
  if(x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
  return strcasecmp(x->name, y->name);
}

void browser_init(browser *b){
  b->items = create_vector((unsigned int) sizeof(dir_item));
  b->filter = FILTER_CRLS;
  b->sel = -1;
  b->scroll = 0;
  if(!getcwd(b->dir, sizeof(b->dir))) strcpy(b->dir, "/");
  browser_load(b);
}

void browser_free(browser *b){
  free_vector(&b->items);
}

void browser_load(browser *b){
  free_vector(&b->items);
  b->items = create_vector((unsigned int) sizeof(dir_item));
  b->sel = -1;
  b->scroll = 0;

  DIR *dir = opendir(b->dir);
  if(!dir){
    set_status(true, "Could not open the folder %s: %s", b->dir, strerror(errno));
    return;
  }

  if(!fs_is_root(b->dir)){
    dir_item up = {"..", true};
    push_back_vector(&b->items, (void *) &up);
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
    if(keep) push_back_vector(&b->items, (void *) &item);
  }
  closedir(dir);

  qsort(b->items.arr, b->items.n, b->items.byte_size, compare_items);
  if(b->items.n) b->sel = 0;
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
  return (dir_item *) get_value_vector(&b->items, index);
}
