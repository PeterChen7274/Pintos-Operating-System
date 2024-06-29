#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdlib.h>
#include <stdint.h>
#include "threads/vaddr.h"

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "lib/kernel/console.h"
#include "devices/input.h"
#include "lib/kernel/list.h"
#include "threads/synch.h"

#include "devices/block.h"

void check_valid_fixed_size_ptr(void* ptr, size_t size, struct intr_frame* f);
void check_valid_string(char* str, struct intr_frame* f);
bool is_valid_ptr(void* ptr);

struct lock filelock;

bool is_valid_ptr(void* ptr) {
  if (ptr == NULL)
    return false;
  if (!is_user_vaddr(ptr))
    return false; // check if less than PHYS_BASE
  return pagedir_get_page(thread_current()->pcb->pagedir, ptr) != NULL;
}

void check_valid_fixed_size_ptr(void* ptr, size_t size, struct intr_frame* f) {
  if (!is_valid_ptr(ptr)) {
    struct process* pcb = thread_current()->pcb;
    pcb->exit_status = -1;
    f->eax = -1;
    process_exit();
    return;
  }
  if (size > 0 && !is_valid_ptr((char*)ptr + size - 1)) {
    struct process* pcb = thread_current()->pcb;
    pcb->exit_status = -1;
    f->eax = -1;
    process_exit();
    return;
  }
}

void check_valid_string(char* str, struct intr_frame* f) {
  do {
    if (!is_valid_ptr(str)) {
      struct process* pcb = thread_current()->pcb;
      pcb->exit_status = -1;
      f->eax = -1;
      process_exit();
      return;
    }
  } while (*(str++) != '\0');
}

struct file* find_file(struct process* p, int fd) {
  if (fd < 3) {
    return NULL;
  }
  struct list_elem* e;
  struct file_descriptor* file_d;
  for (e = list_begin(&(p->file_descriptor_table)); e != list_end(&(p->file_descriptor_table));
       e = list_next(e)) {
    file_d = list_entry(e, struct file_descriptor, elem);
    if (file_d->fd == fd) {
      return file_d->file;
    }
    if (file_d->fd > fd) {
      return NULL; //since fd is strictly increasing, if we go past fd, it means the file hhas been closed so we don't have a file coresponding to fd anymore
    }
  }
  return NULL;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  if (src == NULL || strcmp(src, "") == 0) {
    return 0;
  }
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

struct dir* find_dir(char* addr, struct dir* start) {
  char temp[NAME_MAX + 1];
  int res = get_next_part(temp, &addr);
  struct dir* dir = dir_open_root();
  if (start != NULL) {
    dir->inode = inode_reopen(start->inode);
  }
  struct inode* i;
  while (res != 0) {
    if (!dir_lookup(dir, temp, &i)) {
      return NULL;
    }
    dir->inode = i;
    res = get_next_part(temp, &addr);
  }
  return dir;
}

void splitPath(const char* path, char** directory, char** filename) {
  const char* lastSlash = strrchr(path, '/');

  if (lastSlash != NULL) {
    size_t dirLength = lastSlash - path;
    size_t filenameLength = strlen(lastSlash + 1);

    *directory = (char*)malloc((dirLength + 1) * sizeof(char));
    *filename = (char*)malloc((filenameLength + 1) * sizeof(char));

    strlcpy(*directory, path, dirLength + 1);
    strlcpy(*filename, lastSlash + 1, filenameLength + 1);
  } else {
    *directory = NULL;
    *filename = (char*)malloc((strlen(path) + 1) * sizeof(char));
    strlcpy(*filename, path, strlen(path) + 1);
  }
}

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filelock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  check_valid_fixed_size_ptr(args, sizeof(uint32_t), f); // validate args[0], since check if below

  // process control syscalls
  if (args[0] == SYS_EXIT) {
    check_valid_fixed_size_ptr(args + sizeof(uint32_t), sizeof(uint32_t), f);
    f->eax = args[1];
    struct process* pcb = thread_current()->pcb;
    pcb->exit_status = args[1];
    process_exit();
  } else if (args[0] == SYS_PRACTICE) {
    check_valid_fixed_size_ptr(args + sizeof(uint32_t), sizeof(int), f);
    f->eax = args[1] + 1;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  } else if (args[0] == SYS_EXEC) {
    check_valid_fixed_size_ptr(args + sizeof(uint32_t), sizeof(char*), f);
    check_valid_string((char*)args[1], f);
    lock_acquire(&filelock);
    f->eax = process_execute(args[1]);
    lock_release(&filelock);
  } else if (args[0] == SYS_WAIT) {
    check_valid_fixed_size_ptr(args + sizeof(uint32_t), sizeof(int), f);
    f->eax = process_wait((pid_t)args[1]);
  }

  // file operation syscalls
  else if (args[0] == SYS_CREATE) {
    check_valid_fixed_size_ptr(&args[1], sizeof(char*), f);
    check_valid_string((char*)args[1], f);
    check_valid_fixed_size_ptr(&args[2], sizeof(unsigned), f);
    lock_acquire(&filelock);
    bool res = filesys_create(args[1], args[2]);
    f->eax = res;
    lock_release(&filelock);
  } else if (args[0] == SYS_OPEN) {
    check_valid_fixed_size_ptr(&args[1], sizeof(char*), f);
    check_valid_string((char*)args[1], f);
    lock_acquire(&filelock);
    char* file_name = (char*)args[1];
    if (strcmp(file_name, "") == 0) {
      f->eax = -1;
      lock_release(&filelock);
      return;
    }
    struct file* new_file = NULL;
    struct dir* new_dir = NULL;
    struct process* p = thread_current()->pcb;
    struct file_descriptor* filed = malloc(sizeof(struct file_descriptor));
    struct inode* inode = NULL;
    char* path = (char*)args[1];
    struct dir* addr;
    char* dir;
    char* new;
    splitPath(path, &dir, &new);
    if (path[0] == '/') {
      addr = find_dir(dir, NULL);
    } else {
      addr = find_dir(dir, p->cwd);
    }
    if (strcmp(new, "") == 0) {
      free(new);
      free(dir);
      filed->fd = p->next_fd;
      filed->file = new_file;
      addr->pos = 40;
      filed->dir = addr;
      p->next_fd += 1;
      list_push_back(&(p->file_descriptor_table), &(filed->elem));
      f->eax = filed->fd;
      lock_release(&filelock);
      return;
    }
    if (addr == NULL || addr->inode->removed) {
      f->eax = -1;
      free(filed);
      free(new);
      free(dir);
      lock_release(&filelock);
      return;
    }
    dir_lookup(addr, new, &inode);
    dir_close(addr);
    if (inode == NULL || inode->removed) {
      f->eax = -1;
      free(filed);
      free(new);
      free(dir);
      lock_release(&filelock);
      return;
    }
    if (!inode->data.dir) {
      new_file = file_open(inode);
      filed->d = false;
      if (new_file == NULL) {
        f->eax = -1;
        free(filed);
        free(new);
        free(dir);
        lock_release(&filelock);
        return;
      }
    } else {
      new_dir = dir_open(inode);
      if (new_dir == NULL) {
        f->eax = -1;
        free(filed);
        free(new);
        free(dir);
        lock_release(&filelock);
        return;
      }
      new_dir->pos = 40;
      filed->d = true;
    }
    free(new);
    free(dir);
    filed->fd = p->next_fd;
    filed->file = new_file;
    filed->dir = new_dir;
    p->next_fd += 1;
    list_push_back(&(p->file_descriptor_table), &(filed->elem));
    f->eax = filed->fd;
    lock_release(&filelock);
  } else if (args[0] == SYS_REMOVE) {
    check_valid_fixed_size_ptr(&args[1], sizeof(char*), f);
    check_valid_string((char*)args[1], f);
    lock_acquire(&filelock);
    bool res;
    char* path = args[1];
    struct process* p = thread_current()->pcb;
    struct dir* addr;
    char* dir;
    char* new;
    splitPath(path, &dir, &new);
    if (path[0] == '/') {
      addr = find_dir(dir, NULL);
    } else {
      addr = find_dir(dir, p->cwd);
    }
    if (addr->inode->removed) {
      f->eax = false;
      lock_release(&filelock);
      free(new);
      free(dir);
      return;
    }
    res = dir_remove(addr, new);
    dir_close(addr);
    free(new);
    free(dir);
    f->eax = res;
    lock_release(&filelock);
  } else if (args[0] == SYS_FILESIZE) {
    check_valid_fixed_size_ptr(&args[1], sizeof(int), f);
    lock_acquire(&filelock);
    struct process* p = thread_current()->pcb;
    struct file* file = find_file(p, args[1]);
    if (file == NULL) {
      f->eax = -1;
      lock_release(&filelock);
      return;
    }
    f->eax = file_length(file);
    lock_release(&filelock);
  } else if (args[0] == SYS_READ) {
    check_valid_fixed_size_ptr(&args[2], sizeof(char*), f); // validate pointer to buffer arg
    check_valid_string((char*)args[2], f);                  // validate buffer
    check_valid_fixed_size_ptr(&args[3], sizeof(unsigned), f);
    check_valid_fixed_size_ptr(&args[1], sizeof(int), f);
    lock_acquire(&filelock);
    if (args[1] == STDIN_FILENO) {
      input_getc();
      lock_release(&filelock);
      return;
    }
    //check_valid_string((char*)args[1], f);
    struct process* p = thread_current()->pcb;
    struct file* file = find_file(p, args[1]);
    if (file == NULL) {
      f->eax = -1;
      lock_release(&filelock);
      return;
    }
    f->eax = file_read(file, args[2], (off_t)args[3]);
    lock_release(&filelock);
  } else if (args[0] == SYS_WRITE) {
    check_valid_fixed_size_ptr(&args[2], sizeof(char*), f); // validate pointer to buffer arg
    check_valid_string((char*)args[2], f);                  // validate buffer
    check_valid_fixed_size_ptr(&args[3], sizeof(unsigned), f);
    check_valid_fixed_size_ptr(&args[1], sizeof(int), f);
    lock_acquire(&filelock);
    if (args[1] == STDOUT_FILENO) {
      f->eax = args[3];
      putbuf(args[2], args[3]);
      lock_release(&filelock);
      return;
    }
    //
    struct process* p = thread_current()->pcb;
    struct file* file = find_file(p, args[1]);
    if (file == NULL) {
      struct process* pcb = thread_current()->pcb;
      pcb->exit_status = -1;
      lock_release(&filelock);
      f->eax = -1;
      process_exit();
      return;
    }
    f->eax = file_write(file, args[2], (off_t)args[3]);
    lock_release(&filelock);
  } else if (args[0] == SYS_SEEK) {
    check_valid_fixed_size_ptr(&args[2], sizeof(unsigned), f);
    check_valid_fixed_size_ptr(&args[1], sizeof(int), f);
    lock_acquire(&filelock);
    struct process* p = thread_current()->pcb;
    struct file* file = find_file(p, args[1]);
    if (file == NULL) {
      struct process* pcb = thread_current()->pcb;
      pcb->exit_status = -1;
      lock_release(&filelock);
      f->eax = -1;
      process_exit();
      return;
    }
    file_seek(file, (off_t)args[2]);
    lock_release(&filelock);
  } else if (args[0] == SYS_TELL) {
    check_valid_fixed_size_ptr(&args[1], sizeof(int), f);
    lock_acquire(&filelock);
    struct process* p = thread_current()->pcb;
    struct file* file = find_file(p, args[1]);
    if (file == NULL) {
      struct process* pcb = thread_current()->pcb;
      pcb->exit_status = -1;
      lock_release(&filelock);
      f->eax = -1;
      process_exit();
      return;
    }
    f->eax = file_tell(file);
    lock_release(&filelock);
  } else if (args[0] == SYS_CLOSE) {
    int fd = args[1];
    if (fd < 3) {
      return;
    }
    struct list_elem* e;
    struct file_descriptor* file_d;
    struct process* p = thread_current()->pcb;
    for (e = list_begin(&(p->file_descriptor_table)); e != list_end(&(p->file_descriptor_table));
         e = list_next(e)) {
      file_d = list_entry(e, struct file_descriptor, elem);

      if (file_d->fd == fd) {
        if (file_d->d) {
          dir_close(file_d->dir);
        } else {
          file_close(file_d->file);
        }
        list_remove(e);

        return;
      }
      if (file_d->fd > fd) {
        return;
      }
    }
    return;

  } else if (args[0] == SYS_COMPUTE_E) {
    asm volatile("fsave (%0)" : : "g"(&thread_current()->fpu_state));
    asm volatile("fninit");
    int x = sys_sum_to_e(args[1]);
    f->eax = x;
    asm volatile("frstor (%0)" : : "g"(&thread_current()->fpu_state));
  } else if (args[0] == SYS_MKDIR) {

    char* path = args[1];
    struct process* p = thread_current()->pcb;
    struct dir* addr;
    char* dir;
    char* new;
    splitPath(path, &dir, &new);
    if (path[0] == '/') {
      addr = find_dir(dir, NULL);
    } else {
      addr = find_dir(dir, p->cwd);
    }
    struct inode* i;
    if (addr == NULL || addr->inode->removed || dir_lookup(addr, new, &i)) {
      f->eax = false;
      return;
    }
    block_sector_t bt;
    if (!free_map_allocate(1, &bt)) {
      f->eax = false;
      free(new);
      free(dir);
      return;
    }
    if (!dir_create(bt, 10) || !dir_add(addr, new, bt)) {
      free_map_release(bt, 1);
      f->eax = false;
      free(new);
      free(dir);
      return;
    }
    struct inode* inode = inode_open(bt);
    inode->data.dir = true;
    struct dir* new_dir = malloc(sizeof(struct dir));
    new_dir->inode = inode;
    if (!dir_add(new_dir, ".", bt) || !dir_add(new_dir, "..", inode_get_inumber(addr->inode))) {
      f->eax = false;
    } else {
      f->eax = true;
    }
    free(new_dir);
    free(new);
    free(dir);
    return;
  } else if (args[0] == SYS_CHDIR) {
    struct process* p = thread_current()->pcb;
    struct dir* dir;
    char* path = args[1];
    if (path[0] == '/') {
      dir = find_dir((char*)args[1], NULL);
    } else {
      dir = find_dir((char*)args[1], p->cwd);
    }
    if (dir == NULL) {
      f->eax = false;
      return;
    }
    f->eax = true;
    p->cwd = dir;
    return;
  } else if (args[0] == SYS_ISDIR) {
    int fd = args[1];
    if (fd < 3) {
      f->eax = false;
      return;
    }
    struct list_elem* e;
    struct file_descriptor* file_d;
    struct process* p = thread_current()->pcb;
    for (e = list_begin(&(p->file_descriptor_table)); e != list_end(&(p->file_descriptor_table));
         e = list_next(e)) {
      file_d = list_entry(e, struct file_descriptor, elem);
      if (file_d->fd == fd) {
        f->eax = file_d->d;
        return;
      }
      if (file_d->fd > fd) {
        f->eax = false;
        return;
      }
    }
    f->eax = false;
    return;
  } else if (args[0] == SYS_INUMBER) {
    int fd = args[1];
    if (fd < 3) {
      f->eax = false;
      return;
    }
    struct list_elem* e;
    struct file_descriptor* file_d;
    struct process* p = thread_current()->pcb;
    for (e = list_begin(&(p->file_descriptor_table)); e != list_end(&(p->file_descriptor_table));
         e = list_next(e)) {
      file_d = list_entry(e, struct file_descriptor, elem);
      if (file_d->fd == fd) {
        if (file_d->d) {
          f->eax = inode_get_inumber(file_d->dir->inode);
        } else {
          f->eax = inode_get_inumber(file_d->file->inode);
        }
        return;
      }
      if (file_d->fd > fd) {
        f->eax = false;
        return;
      }
    }
    f->eax = false;
    return;
  } else if (args[0] == SYS_READDIR) {
    int fd = args[1];
    if (fd < 3) {
      f->eax = false;
      return;
    }
    struct list_elem* e;
    struct file_descriptor* file_d;
    struct process* p = thread_current()->pcb;
    for (e = list_begin(&(p->file_descriptor_table)); e != list_end(&(p->file_descriptor_table));
         e = list_next(e)) {
      file_d = list_entry(e, struct file_descriptor, elem);
      if (file_d->fd == fd) {
        if (file_d->d) {
          f->eax = dir_readdir(file_d->dir, (char*)args[2]);
        } else {
          f->eax = false;
        }
        return;
      }
      if (file_d->fd > fd) {
        f->eax = false;
        return;
      }
    }
    f->eax = false;
    return;
  } else if (args[0] == SYS_CACHE_STATS) {
    f->eax = get_buffer_cache_hit_rate();
    return;
  } else if (args[0] == SYS_RESET_CACHE_STATS) {
    reset_buffer_cache_stats();
    f->eax = true;
    return;
  } else if (args[0] == SYS_RESET_CACHE) {
    buffer_cache_reset();
    f->eax = true;
    return;
  } else if (args[0] == SYS_READ_COUNT) {
    struct block* device = block_get_role(BLOCK_FILESYS);
    f->eax = block_get_read_count(device);
    return;
  } else if (args[0] == SYS_WRITE_COUNT) {
    struct block* device = block_get_role(BLOCK_FILESYS);
    f->eax = block_get_write_count(device);
    return;
  }
}