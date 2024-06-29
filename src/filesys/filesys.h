#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block* fs_device;

struct buffer_cache_entry {
  block_sector_t sector;
  bool valid;
  bool dirty;
  bool accessed;
  void* data;
  struct lock lock;
  struct list_elem elem;
};

void buffer_cache_read(block_sector_t, void*, off_t, off_t);
void buffer_cache_write(block_sector_t, void*, off_t, off_t);
void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size);
struct file* filesys_open(const char* name);
bool filesys_remove(const char* name);

int get_buffer_cache_hit_rate();
void reset_buffer_cache_stats();

#endif /* filesys/filesys.h */
