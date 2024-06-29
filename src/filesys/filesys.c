#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

struct list buffer_cache;
struct lock buffer_cache_lock;
struct buffer_cache_entry* clock_hand;

int hit_count = 0;
int miss_count = 0;

static void do_format(void);

void buffer_cache_init(void) {
  list_init(&buffer_cache);
  lock_init(&buffer_cache_lock);
  lock_acquire(&buffer_cache_lock);

  struct buffer_cache_entry* entry;
  for (int i = 0; i < 64; i++) {
    entry = malloc(sizeof(struct buffer_cache_entry));
    if (entry == NULL)
      PANIC("Failed to malloc buffer cache entry");
    entry->data = malloc(BLOCK_SECTOR_SIZE);
    if (entry->data == NULL)
      PANIC("Failed to malloc buffer cache entry data");
    entry->sector = -1;
    entry->dirty = false;
    entry->accessed = false;
    entry->valid = false;
    lock_init(&entry->lock);
    list_push_back(&buffer_cache, &entry->elem);
  }

  clock_hand = list_entry(list_begin(&buffer_cache), struct buffer_cache_entry, elem);
  lock_release(&buffer_cache_lock);
}

struct buffer_cache_entry* find_buffer_cache_entry(block_sector_t sector) {
  struct list_elem* e;
  struct buffer_cache_entry* entry;

  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)) {
    entry = list_entry(e, struct buffer_cache_entry, elem);
    if (entry->sector == sector && entry->valid) {
      return entry;
    }
  }
  return NULL;
}

struct buffer_cache_entry* load_new_entry(block_sector_t sector) {
  struct buffer_cache_entry* entry;
  struct list_elem* e;

  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)) {
    entry = list_entry(e, struct buffer_cache_entry, elem);
    if (!entry->valid) {
      entry->sector = sector;
      entry->valid = true;
      entry->dirty = false;
      entry->accessed = true;
      lock_release(&buffer_cache_lock);
      block_read(fs_device, sector, entry->data);
      lock_acquire(&buffer_cache_lock);
      return entry;
    }
  }

  while (true) {
    entry = clock_hand;
    if (!entry->accessed) {
      if (entry->dirty) {
        lock_release(&buffer_cache_lock);
        lock_acquire(&entry->lock);
        block_write(fs_device, entry->sector, entry->data);
        lock_release(&entry->lock);
        lock_acquire(&buffer_cache_lock);
      }

      entry->sector = sector;
      entry->valid = true;
      entry->dirty = false;
      entry->accessed = true;

      lock_release(&buffer_cache_lock);
      block_read(fs_device, sector, entry->data);
      lock_acquire(&buffer_cache_lock);

      e = list_next(&clock_hand->elem);
      if (e == list_end(&buffer_cache)) {
        clock_hand = list_entry(list_begin(&buffer_cache), struct buffer_cache_entry, elem);
      } else {
        clock_hand = list_entry(e, struct buffer_cache_entry, elem);
      }
      return entry;
    } else {
      entry->accessed = false;
      e = list_next(&clock_hand->elem);
      if (e == list_end(&buffer_cache)) {
        clock_hand = list_entry(list_begin(&buffer_cache), struct buffer_cache_entry, elem);
      } else {
        clock_hand = list_entry(e, struct buffer_cache_entry, elem);
      }
    }
  }
}

void buffer_cache_read(block_sector_t sector, void* buffer_, off_t size, off_t offset) {
  struct buffer_cache_entry* entry;
  lock_acquire(&buffer_cache_lock);

  entry = find_buffer_cache_entry(sector);
  if (entry != NULL) {
    memcpy(buffer_, entry->data + offset, size);
    entry->accessed = true;
    hit_count += 1;
  } else {
    entry = load_new_entry(sector);
    memcpy(buffer_, entry->data + offset, size);
    miss_count += 1;
  }
  lock_release(&buffer_cache_lock);
}

struct buffer_cache_entry* write_new_entry(block_sector_t sector) {
  struct buffer_cache_entry* entry;
  struct list_elem* e;

  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)) {
    entry = list_entry(e, struct buffer_cache_entry, elem);
    if (!entry->valid) {
      entry->sector = sector;
      entry->valid = true;
      entry->dirty = false;
      entry->accessed = true;
      return entry;
    }
  }

  while (true) {
    entry = clock_hand;
    if (!entry->accessed) {
      if (entry->dirty) {
        lock_release(&buffer_cache_lock);
        lock_acquire(&entry->lock);
        block_write(fs_device, entry->sector, entry->data);
        lock_release(&entry->lock);
        lock_acquire(&buffer_cache_lock);
      }

      entry->sector = sector;
      entry->valid = true;
      entry->dirty = false;
      entry->accessed = true;

      e = list_next(&clock_hand->elem);
      if (e == list_end(&buffer_cache)) {
        clock_hand = list_entry(list_begin(&buffer_cache), struct buffer_cache_entry, elem);
      } else {
        clock_hand = list_entry(e, struct buffer_cache_entry, elem);
      }
      return entry;
    } else {
      entry->accessed = false;
      e = list_next(&clock_hand->elem);
      if (e == list_end(&buffer_cache)) {
        clock_hand = list_entry(list_begin(&buffer_cache), struct buffer_cache_entry, elem);
      } else {
        clock_hand = list_entry(e, struct buffer_cache_entry, elem);
      }
    }
  }
}

void buffer_cache_write(block_sector_t sector, void* buffer_, off_t size, off_t offset) {
  struct buffer_cache_entry* entry;
  lock_acquire(&buffer_cache_lock);

  entry = find_buffer_cache_entry(sector);
  if (entry != NULL) {
    memcpy(entry->data + offset, buffer_, size);
    entry->accessed = true;
    entry->dirty = true;
  } else {
    entry = write_new_entry(sector);
    memcpy(entry->data + offset, buffer_, size);
    entry->dirty = true;
  }
  lock_release(&buffer_cache_lock);
}

void buffer_cache_flush_all_entries(void) {
  struct list_elem* e;
  struct buffer_cache_entry* entry;

  lock_acquire(&buffer_cache_lock);
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)) {
    entry = list_entry(e, struct buffer_cache_entry, elem);

    if (entry->valid && entry->dirty) {
      lock_release(&buffer_cache_lock);

      lock_acquire(&entry->lock);
      block_write(fs_device, entry->sector, entry->data);
      lock_release(&entry->lock);

      lock_acquire(&buffer_cache_lock);
      entry->dirty = false;
    }
  }
  lock_release(&buffer_cache_lock);
}

void buffer_cache_reset(void) {
  buffer_cache_flush_all_entries();
  lock_acquire(&buffer_cache_lock);
  struct list_elem* e;
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)) {
    struct buffer_cache_entry* entry = list_entry(e, struct buffer_cache_entry, elem);
    entry->valid = false;
    entry->dirty = false;
    entry->accessed = false;
  }
  lock_release(&buffer_cache_lock);
}

int get_buffer_cache_hit_rate() { return hit_count / (hit_count + miss_count); }

void reset_buffer_cache_stats() {
  hit_count = 0;
  miss_count = 0;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();
  buffer_cache_init();

  if (format)
    do_format();

  free_map_open();
  struct dir* dir = dir_open_root();
  dir_add(dir, ".", inode_get_inumber(dir->inode));
  dir_add(dir, "..", inode_get_inumber(dir->inode));
  dir_close(dir);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  free_map_close();
  buffer_cache_flush_all_entries();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  block_sector_t inode_sector = 0;
  struct process* p = thread_current()->pcb;
  struct dir* dir;
  char* addr;
  char* new;
  splitPath(name, &addr, &new);
  if (name[0] == '/') {
    dir = find_dir(addr, NULL);
  } else {
    dir = find_dir(addr, p->cwd);
  }
  if (dir == NULL || dir->inode->removed) {
    dir_close(dir);
    free(addr);
    free(new);
    return false;
  }
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size) && dir_add(dir, new, inode_sector));
  if (!success && inode_sector != 0) {
    free_map_release(inode_sector, 1);
  }
  dir_close(dir);
  free(addr);
  free(new);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct process* p = thread_current()->pcb;
  struct dir* dir;
  char* addr;
  char* new;
  splitPath(name, &addr, &new);
  if (name[0] == '/') {
    dir = find_dir(addr, NULL);
  } else {
    dir = find_dir(addr, p->cwd);
  }
  struct inode* inode = NULL;
  if (dir == NULL || dir->inode->removed) {
    dir_close(dir);
    free(addr);
    free(new);
    return NULL;
  }
  if (dir != NULL) {
    dir_lookup(dir, new, &inode);
  }
  dir_close(dir);
  free(addr);
  free(new);
  return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
  struct process* p = thread_current()->pcb;
  struct dir* dir;
  char* addr;
  char* new;
  splitPath(name, &addr, &new);
  if (name[0] == '/') {
    dir = find_dir(addr, NULL);
  } else {
    dir = find_dir(addr, p->cwd);
  }
  if (dir == NULL || dir->inode->removed) {
    dir_close(dir);
    free(addr);
    free(new);
    return false;
  }
  bool success = dir != NULL && dir_remove(dir, new);
  dir_close(dir);
  free(addr);
  free(new);

  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}
