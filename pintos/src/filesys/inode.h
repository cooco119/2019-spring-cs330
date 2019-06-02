#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"

struct bitmap;

struct buffer_cache_entry
{
  disk_sector_t idx;
  uint8_t *data;
  bool dirty;
  bool accessed;
  bool empty;
  struct list_elem elem;
};
static struct semaphore write_behind_lock;

void inode_init (void);
bool inode_create (disk_sector_t, off_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
void write_behind_helper(struct thread *);
void write_dirty_inodes ();
void free_buffer_cache ();
struct buffer_cache_entry * check_cache (disk_sector_t idx);
bool fetch_sector (disk_sector_t idx);
bool evict_sector (disk_sector_t idx);
bool fetch_cache (disk_sector_t idx, void *buffer_, off_t size, off_t origin_ofs, off_t target_ofs);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
