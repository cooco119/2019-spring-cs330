#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include <bitmap.h>
#include <stdio.h>
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define BUFFER_CACHE_SIZE 64

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / DISK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct list buffer_cache;

static int buffer_cache_cnt = 0;

static struct bitmap *buffer_cache_map;

static struct lock evict_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  list_init (&buffer_cache);
  lock_init (&evict_lock);
  buffer_cache_map = bitmap_create(BUFFER_CACHE_SIZE);
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    struct buffer_cache_entry *c = (struct buffer_cache_entry*) malloc(sizeof(struct buffer_cache_entry));
    c->empty = true;
    c->idx = NULL;
    c->accessed = false;
    c->dirty = false;
    c->data = malloc(DISK_SECTOR_SIZE);
    list_push_back(&buffer_cache, &c->elem);
    // printf("Added entry %d of idx %s at mem of %p, elem: %p\n", i, c->idx == NULL ? "NULL" : "error", c, c->elem);
  }
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[DISK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                disk_write (filesys_disk, disk_inode->start + i, zeros); 
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  disk_read (filesys_disk, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Find target SECTOR_IDX in buffer cache. If doesn't exists, 
   returns NULL. */
struct buffer_cache_entry *
check_cache (disk_sector_t idx)
{
  struct list_elem *e;
  struct buffer_cache_entry *c;
  int i = -1;

  // printf("Bitmap status: size %d\n", bitmap_scan(buffer_cache_map, 0, 1, false));
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e))
  {
    i++;
    c = list_entry(e, struct buffer_cache_entry, elem);
    if (c->empty == true)
    {
    // printf("passing empty cache %d of mempos %p, elem: %p\n", i, c, c->elem);
      continue;
    }
    if (c->idx == idx)
    {
      return c;
    }
    // printf("passing used cache %d of idx %d\n", i, c->idx == NULL ? -1 : c->idx);
  }

  return NULL;
}

disk_sector_t
pick_entry_to_evict ()
{
  struct list_elem *e;
  struct buffer_cache_entry *c;
  int i = 0;
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next(e))
  {
    c = list_entry(e, struct buffer_cache_entry, elem);
    // printf("i: %d, idx: %d, accessed: %s\n", i, c->idx, c->accessed ? "true" : "false");
    if (! c->accessed)
    {
      return c->idx;
    }
    c->accessed = false;
  }
  
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next(e))
  {
    c = list_entry(e, struct buffer_cache_entry, elem);
    if (! c->accessed)
    {
      return c->idx;
    }
    c->accessed = false;
  }
}

/* Fetch data from SECTOR_IDX of sector into buffer_cache_entry and
   push the entry into the buffer_cache_list. */
bool
fetch_sector (disk_sector_t idx)
{
  // printf("Fetching sector of %d\n", idx);
  // printf("Buffer count %d\n", buffer_cache_cnt);
  // struct buffer_cache_entry *c = (struct buffer_cache_entry*) malloc(sizeof(struct buffer_cache_entry));
  int empty_cache_pos;
  struct buffer_cache_entry *c;
  struct list_elem *e;
  int i = 0;
  if (buffer_cache_cnt < BUFFER_CACHE_SIZE)
  {
    for (e = list_begin(&buffer_cache); i < BUFFER_CACHE_SIZE && e != list_end(&buffer_cache); e = list_next(e))
    {
      // printf("i: %d, mempos of elem %p, elem->next %p\n", i, e, e->next);
      c = list_entry(e, struct buffer_cache_entry, elem);
      if (c->empty)
      {
        // printf("Got empty buffer_cache_entry\n");
        break;
      }
      // printf("iterating...\n");
      i++;
    }
    c->idx = idx;
    c->accessed = false;
    c->dirty = false;
    c->empty = false;
    empty_cache_pos = bitmap_scan_and_flip(buffer_cache_map, i, 1, false);
    // printf("before reading, check of elem: %p\n", c->elem);
    disk_read(filesys_disk, idx, c->data);
    // printf("i: %d, Read data into %p, check of elem: %p elem->next: %p\n", i, c->data, c->elem, &c->elem.next);
    // list_push_back(&buffer_cache, &c->elem);
    // printf("\t sector %d disk read result\n", idx);
    // hex_dump(c->data, c->data, DISK_SECTOR_SIZE, 0);
    buffer_cache_cnt++;
    
    return true;
  }
  else
  {
    // TODO: select which sector to evict
    disk_sector_t target = pick_entry_to_evict ();
    evict_sector (target); 
    return fetch_sector (idx);
  }
  return false;
}

/* Evicts sector from buffer_cache_list */
bool
evict_sector (disk_sector_t idx)
{
  lock_acquire (&evict_lock);
  struct list_elem *e;
  struct buffer_cache_entry *c;
  int i = 0;
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e))
  {
    c = list_entry(e, struct buffer_cache_entry, elem);
    if (c->idx == idx)
    {
      list_remove (e);
      bitmap_set(buffer_cache_map, i, false);
      disk_write (filesys_disk, c->idx, c->data);
      free (c);
      buffer_cache_cnt--;
      lock_release (&evict_lock);
      // printf("evicting %d\n", idx);
      return true;
    }
    i++;
  }

  lock_release (&evict_lock);
  return false;
}

/* Fetch SIZE bytes from cache into BUFFER from OFFSET.
   Returns the number of bytes acually read. */
bool
fetch_cache (disk_sector_t idx, void *buffer_, off_t size, off_t origin_ofs, off_t target_ofs)
{
  uint8_t *buffer = buffer_;
  struct buffer_cache_entry* cache_entry = check_cache(idx);

  // printf("Bitmap status: size %d\n", bitmap_scan(buffer_cache_map, 0, 1, false));
  if (cache_entry != NULL)
  {
    cache_entry->accessed = true;
    memcpy(buffer + target_ofs, cache_entry->data + origin_ofs, size);
    // printf("copy size %d data of %p into buffer of %p\n", size, cache_entry->data + origin_ofs, buffer);
    // printf("&&&&&&&& cache range: %p ~ %p\n", cache_entry->data, cache_entry->data + DISK_SECTOR_SIZE);
    return true;
  }
  else
  {
    if (fetch_sector (idx))
    {
      return fetch_cache (idx, buffer, size, origin_ofs, target_ofs);
    }
    else 
    {
      return false;
    }
  }
}

bool
commit_cache (disk_sector_t idx, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  struct buffer_cache_entry* cache_entry = check_cache(idx);

  if (cache_entry != NULL)
  {
    memcpy (cache_entry->data + offset, buffer, size);
    disk_write(filesys_disk, idx, cache_entry->data);
    cache_entry->dirty = true;
    return true;
  }
  else
  {
    if (fetch_sector(idx))
    {
      return commit_cache(idx, buffer, size, offset);
    }
  }
  return false;
}


/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  // uint8_t *tmp_buf ;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  // off_t tmp;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      fetch_cache(sector_idx, buffer, chunk_size, sector_ofs, bytes_read);

      // if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     // fetch_cache(sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
      //     tmp_buf = malloc(DISK_SECTOR_SIZE);
      //     memcpy(tmp_buf, buffer + bytes_read, DISK_SECTOR_SIZE);
      //     disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
      //     if (tmp = memcmp(tmp_buf, buffer + bytes_read, DISK_SECTOR_SIZE) != 0)
      //     {
      //       printf("val in cache: %x, val in disk %x\n", tmp_buf + tmp, buffer + bytes_read + tmp);
      //     }
      //     printf("copy size %d data into buffer of %p\n", chunk_size, buffer + bytes_read);
      //     free(tmp_buf);
      //   }
      // else 
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (DISK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     tmp_buf = malloc(DISK_SECTOR_SIZE);
      //     fetch_cache(sector_idx, tmp_buf, DISK_SECTOR_SIZE, 0, 0);
      //     // fetch_cache(sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
      //     disk_read (filesys_disk, sector_idx, bounce);
      //     printf("!!!!!!!!! bounce range: %p ~ %p\n", bounce, bounce + DISK_SECTOR_SIZE);
      //     printf("sector idx: %d\n", sector_idx);
          
      //       // printf("\n\n<tmp_buf>\n");
      //       // hex_dump(tmp_buf, tmp_buf, 32, false);
      //       // printf("\n\n<bounce>\n");
      //       // hex_dump(bounce, bounce, 32, false);
      //       // printf("\n\n");
      //     if (tmp = memcmp(tmp_buf, bounce, DISK_SECTOR_SIZE) != 0)
      //     {
      //       // printf("\t\t\tval in buffer: %x, val in bounce: %x\n", *((char*)(tmp_buf + tmp)), *((char*)(bounce + tmp)));
      //       printf("\n\n<tmp_buf>\n");
      //       hex_dump(tmp_buf, tmp_buf, 32, false);
      //       printf("\n\n<bounce>\n");
      //       hex_dump(bounce, bounce, 32, false);
      //       printf("\n\n");
      //     }
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //     printf("copy size %d data of %p into buffer of %p\n", chunk_size, bounce + sector_ofs, buffer + bytes_read);
      //   }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  // free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      commit_cache(sector_idx, buffer + bytes_written, chunk_size, sector_ofs);
      // printf("\t\t\t\t\tWriting in sector %d, size %d\n", sector_idx, chunk_size);
      // if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
      //   {
      //     /* Write full sector directly to disk. */
      //     disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
      //   }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (DISK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       disk_read (filesys_disk, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, DISK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     disk_write (filesys_disk, sector_idx, bounce); 
      //   }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
