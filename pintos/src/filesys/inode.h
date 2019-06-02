#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"

struct bitmap;

struct indirect_block
{
  disk_sector_t pointers[128];
};

struct inode_disk
  {
    // disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    disk_sector_t direct_pointers[12];
    disk_sector_t indirect;
    disk_sector_t doubly_indirect;
    uint32_t unused[112];               /* Not used. */
  };

struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct list allocated_blocks;
  };

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
bool commit_cache (disk_sector_t idx, void *buffer_, off_t size, off_t offset);
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
