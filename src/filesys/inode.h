#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define NUM_DIRECT 100

struct bitmap;
struct inode_disk {
  block_sector_t direct[NUM_DIRECT]; /* First data sector. */
  block_sector_t indirect;
  block_sector_t double_indirect;
  off_t length;   /* File size in bytes. */
  unsigned magic; /* Magic number. */
  bool dir;
  char a;
  char b;
  char c;
  uint32_t unused[23]; /* Not used. */
};
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

void inode_init(void);
bool inode_create(block_sector_t, off_t);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(const struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
off_t inode_length(const struct inode*);
bool inode_resize(struct inode_disk* id, off_t size);
bool inode_dealloc(struct inode_disk* id);

#endif /* filesys/inode.h */
