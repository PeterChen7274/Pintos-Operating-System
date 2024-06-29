#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

bool inode_resize(struct inode_disk* id, off_t size) {

  static char zeros[BLOCK_SECTOR_SIZE];

  //Handle Direct Pointers
  for (int i = 0; i < NUM_DIRECT; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && id->direct[i] != 0) {
      free_map_release(id->direct[i], 1);
      id->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && id->direct[i] == 0) {
      if (!free_map_allocate(1, &id->direct[i])) {
        return false;
      }
      buffer_cache_write(id->direct[i], zeros, BLOCK_SECTOR_SIZE, 0);
    }
  }

  //Check if Indirect Pointers needed
  if (id->indirect == 0 && size <= NUM_DIRECT * BLOCK_SECTOR_SIZE) {
    id->length = size;
    return true;
  }

  block_sector_t buffer[128];
  memset(buffer, 0, 512);

  if (id->indirect == 0) {
    if (!free_map_allocate(1, &id->indirect)) {
      return false;
    }
    buffer_cache_write(id->indirect, zeros, BLOCK_SECTOR_SIZE, 0);
  } else {
    buffer_cache_read(id->indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    //block_read(fs_device, id->indirect, buffer); //TODO?
  }

  //Handle Indirect Pointers
  for (int i = 0; i < 128; i++) {
    if (size <= (NUM_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else if (size > (NUM_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      if (!free_map_allocate(1, &buffer[i])) {
        return false;
      }
      buffer_cache_write(buffer[i], zeros, BLOCK_SECTOR_SIZE, 0);
    }
  }

  buffer_cache_write(id->indirect, buffer, BLOCK_SECTOR_SIZE, 0);
  //block_write(fs_device, id->indirect, buffer);

  if (size <= NUM_DIRECT * BLOCK_SECTOR_SIZE) {
    free_map_release(id->indirect, 1);
    id->indirect = 0;
    // id->length = size;
    // return true;
  }

  //Check if Double Indirect Pointers needed
  if (id->double_indirect == 0 && size <= (NUM_DIRECT + 128) * BLOCK_SECTOR_SIZE) {
    id->length = size;
    return true;
  }

  memset(buffer, 0, 512);
  block_sector_t indirect_buffer[128];
  memset(indirect_buffer, 0, 512);

  if (id->double_indirect == 0) {
    if (!free_map_allocate(1, &id->double_indirect)) {
      return false;
    }
    buffer_cache_write(id->double_indirect, zeros, BLOCK_SECTOR_SIZE, 0);
  } else {
    buffer_cache_read(id->double_indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    //block_read(fs_device, id->double_indirect, buffer);
  }

  for (int i = 0; i < 128; i++) {

    memset(indirect_buffer, 0, 512);

    if (buffer[i] != 0) {
      //SHRINK. first get indirect block, then free all the direct blocks within indirect block, then free indirect block

      buffer_cache_read(buffer[i], indirect_buffer, BLOCK_SECTOR_SIZE, 0);
      //block_read(fs_device, buffer[i], indirect_buffer);

      for (int j = 0; j < 128; j++) {
        if (size <= (NUM_DIRECT + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
            indirect_buffer[j] != 0) {
          free_map_release(indirect_buffer[j], 1);
          indirect_buffer[j] = 0;
        } else if (size > (NUM_DIRECT + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
                   indirect_buffer[j] == 0) {
          if (!free_map_allocate(1, &indirect_buffer[j])) {
            return false;
          }
          buffer_cache_write(indirect_buffer[j], zeros, BLOCK_SECTOR_SIZE, 0);
        }
      }

      buffer_cache_write(buffer[i], indirect_buffer, BLOCK_SECTOR_SIZE, 0);
      //block_write(fs_device, buffer[i], indirect_buffer);

      if (size <= (NUM_DIRECT + 128 + 128 * i) * BLOCK_SECTOR_SIZE) {
        free_map_release(buffer[i], 1);
        buffer[i] = 0;
      }

    } else if (buffer[i] == 0) {
      //GROW. first allocate indirect block. Then start writing to the direct blocks.

      if (size > (NUM_DIRECT + 128 + 128 * i) * BLOCK_SECTOR_SIZE) {
        if (!free_map_allocate(1, &buffer[i])) {
          return false;
        }
        buffer_cache_write(buffer[i], zeros, BLOCK_SECTOR_SIZE, 0);
      } else {
        continue;
      }

      for (int j = 0; j < 128; j++) {
        if (size <= (NUM_DIRECT + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
            indirect_buffer[j] != 0) {
          free_map_release(indirect_buffer[j], 1);
          indirect_buffer[j] = 0;
        } else if (size > (NUM_DIRECT + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
                   indirect_buffer[j] == 0) {
          if (!free_map_allocate(1, &indirect_buffer[j])) {
            return false;
          }
          buffer_cache_write(indirect_buffer[j], zeros, BLOCK_SECTOR_SIZE, 0);
        }
      }

      buffer_cache_write(buffer[i], indirect_buffer, BLOCK_SECTOR_SIZE, 0);
      //block_write(fs_device, buffer[i], indirect_buffer);
    }
  }

  buffer_cache_write(id->double_indirect, buffer, BLOCK_SECTOR_SIZE, 0);
  //block_write(fs_device, id->double_indirect, buffer);

  //Check if Double Indirect Pointers needed
  if (id->double_indirect != 0 && size <= (NUM_DIRECT + 128) * BLOCK_SECTOR_SIZE) {
    free_map_release(id->double_indirect, 1);
    id->double_indirect = 0;
  }

  id->length = size;
  return true;
}

bool inode_dealloc(struct inode_disk* id) {

  //Free direct pointers
  for (int i = 0; i < NUM_DIRECT; i++) {
    if (id->direct[i] != 0) {
      free_map_release(id->direct[i], 1);
      id->direct[i] = 0;
    }
  }

  block_sector_t buffer[128];
  memset(buffer, 0, 512);

  //Free indirect pointers
  if (id->indirect != 0) {
    buffer_cache_read(id->indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    //block_read(fs_device, id->indirect, buffer);

    for (int i = 0; i < 128; i++) {
      if (buffer[i] != 0) {
        free_map_release(buffer[i], 1);
        buffer[i] = 0;
      }
    }
  }

  memset(buffer, 0, 512);
  block_sector_t double_buffer[128];
  memset(double_buffer, 0, 512);

  //Free Double Indirect Pointers
  if (id->double_indirect != 0) {

    buffer_cache_read(id->double_indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    //block_read(fs_device, id->double_indirect, buffer);

    for (int i = 0; i < 128; i++) {
      if (buffer[i] != 0) {

        buffer_cache_read(buffer[i], double_buffer, BLOCK_SECTOR_SIZE, 0);
        //block_read(fs_device, buffer[i], double_buffer);

        for (int j = 0; j < 128; j++) {
          if (double_buffer[j] != 0) {
            free_map_release(double_buffer[j], 1);
            double_buffer[j] = 0;
          }
        }

        free_map_release(buffer[i], 1);
        buffer[i] = 0;
      }
    }
  }

  //Check if Double Indirect Pointers needed
  if (id->double_indirect != 0) {
    free_map_release(id->double_indirect, 1);
    id->double_indirect = 0;
  }

  //Check if Double Indirect Pointers needed
  if (id->indirect != 0) {
    free_map_release(id->indirect, 1);
    id->indirect = 0;
  }

  id->length = 0;

  return true;
}

/* In-memory inode. */

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);

  int sector_num = pos / BLOCK_SECTOR_SIZE;
  if (pos >= inode->data.length || pos < 0) {
    return -1;
  }

  if (sector_num < NUM_DIRECT) {
    return inode->data.direct[sector_num];

  } else if (sector_num < NUM_DIRECT + 128) {
    //Read in indirect block and return the block sector of the right one
    block_sector_t buffer[128];
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    buffer_cache_read(inode->data.indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    return buffer[sector_num - NUM_DIRECT];

  } else {
    /*First read in double indirect block. Then figure out which indirect block to find and read that in. 
    Finally, return the sector for the direct block. */
    block_sector_t buffer[128];
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    buffer_cache_read(inode->data.double_indirect, buffer, BLOCK_SECTOR_SIZE, 0);

    //find the indirect block needed
    block_sector_t indirect_buffer[128];
    memset(indirect_buffer, 0, BLOCK_SECTOR_SIZE);
    int indirect_pos = (sector_num - NUM_DIRECT - 128) / 128;

    buffer_cache_read(buffer[indirect_pos], indirect_buffer, BLOCK_SECTOR_SIZE, 0);
    return indirect_buffer[(sector_num - NUM_DIRECT - 128) % 128];
  }
}

/* Returns the block device sector that contains byte offset POS
   within INODE_DISK (id).
   Returns -1 if INODE_DISK(id) does not contain data for a byte at offset
   POS. 
   This is different from byte_to_sector as this takes in an inode_disk struct */
static block_sector_t byte_to_sector_inode_disk(struct inode_disk* id, off_t pos) {
  ASSERT(id != NULL);

  int sector_num = pos / BLOCK_SECTOR_SIZE;
  if (pos >= id->length || pos < 0) {
    return -1;
  }

  if (sector_num < NUM_DIRECT) {
    return id->direct[sector_num];

  } else if (sector_num < NUM_DIRECT + 128) {
    //Read in indirect block and return the block sector of the right one
    block_sector_t buffer[128];
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    buffer_cache_read(id->indirect, buffer, BLOCK_SECTOR_SIZE, 0);
    return buffer[sector_num - NUM_DIRECT];

  } else {
    /*First read in double indirect block. Then figure out which indirect block to find and read that in. 
    Finally, return the sector for the direct block. */
    block_sector_t buffer[128];
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    buffer_cache_read(id->double_indirect, buffer, BLOCK_SECTOR_SIZE, 0);

    //find the indirect block needed
    block_sector_t indirect_buffer[128];
    memset(indirect_buffer, 0, BLOCK_SECTOR_SIZE);
    int indirect_pos = (sector_num - NUM_DIRECT - 128) / 128;

    buffer_cache_read(buffer[indirect_pos], indirect_buffer, BLOCK_SECTOR_SIZE, 0);
    return indirect_buffer[(sector_num - NUM_DIRECT - 128) % 128];
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    //initializing the data pointers for inode to all be 0
    for (int i = 0; i < NUM_DIRECT; i++) {
      disk_inode->direct[i] = 0;
    }
    disk_inode->indirect = 0;
    disk_inode->double_indirect = 0;
    disk_inode->length = 0;
    disk_inode->magic = INODE_MAGIC;

    if (!inode_resize(disk_inode, length)) {
      free(disk_inode);
      return false;
    }
    struct inode* dummy = malloc(5 * sizeof(struct inode));
    if (dummy == NULL) {
      free(disk_inode);
      return false;
    }
    free(dummy);

    static char zeros[BLOCK_SECTOR_SIZE];
    for (size_t i = 0; i < sectors; i++) {
      block_sector_t temp_block = byte_to_sector_inode_disk(disk_inode, i * BLOCK_SECTOR_SIZE);
      buffer_cache_write(temp_block, zeros, BLOCK_SECTOR_SIZE, 0);
    }

    buffer_cache_write(sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
    success = true;

    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //block_read(fs_device, inode->sector, &inode->data);
  buffer_cache_read(inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      inode_dealloc(&inode->data);
      free_map_release(inode->sector, 1);
    }

    free(inode); // TODO: PROB SOME OOM STUFF HERE
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  //TODO: Shrink the inode to 0 to free all blocks
  ASSERT(inode != NULL);
  inode->removed = true;
  if (inode->open_cnt == 0) {
    list_remove(&inode->elem);
    inode_dealloc(&inode->data);
    free_map_release(inode->sector, 1);
    free(inode); // TODO: PROB SOME OOM STUFF HERE
  }
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t* bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0) {
      break;
    }

    buffer_cache_read(sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
    //block_read(fs_device, sector_idx, buffer_);

    //if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    /* Read full sector directly into caller's buffer. */
    //block_read(fs_device, sector_idx, buffer + bytes_read);
    //} else {
    /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
    //  if (bounce == NULL) {
    //    bounce = malloc(BLOCK_SECTOR_SIZE);
    //    if (bounce == NULL)
    //      break;
    //  }
    //  block_read(fs_device, sector_idx, bounce);
    //  memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    //}

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  //free(bounce);

  if (bytes_read == 0) {
    return 0;
  }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t* bounce = NULL;
  //msg("ASDASDASDASDASDAS, %d\n", inode->data.direct[0]);
  if (offset + size > inode->data.length) {
    if (!inode_resize(&inode->data, size + offset)) {
      return 0;
    }
  }

  //msg("ASDASDASDASDASDAS, %d\n", inode->data.direct[0]);

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    buffer_cache_write(sector_idx, buffer + bytes_written, chunk_size, sector_ofs);
    //if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    /* Write full sector directly to disk. */
    //  block_write(fs_device, sector_idx, buffer + bytes_written);
    //} else {
    /* We need a bounce buffer. */
    //  if (bounce == NULL) {
    //    bounce = malloc(BLOCK_SECTOR_SIZE);
    //    if (bounce == NULL)
    //      break;
    //  }

    /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
    //  if (sector_ofs > 0 || chunk_size < sector_left)
    //    block_read(fs_device, sector_idx, bounce);
    //  else
    //    memset(bounce, 0, BLOCK_SECTOR_SIZE);
    //  memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
    //  block_write(fs_device, sector_idx, bounce);
    //}

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  //free(bounce);

  buffer_cache_write(inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) { return inode->data.length; }
