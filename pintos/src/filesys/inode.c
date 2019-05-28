#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define BUFFER_CACHE_SIZE 64
#define DIRECT_POINTER_REGION DISK_SECTOR_SIZE * 12
#define INDIRECT_POINTER_REGION DIRECT_POINTER_REGION + (DISK_SECTOR_SIZE * 128)
#define DOUBLY_INDIRECT_REGION INDIRECT_POINTER_REGION + (DISK_SECTOR_SIZE * 128 * 128)
#define INDIRECT_BLOCK_SIZE 128

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
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

struct indirect_block
{
  disk_sector_t pointers[128];
};

struct allocated_blocks_entry
{
  disk_sector_t idx;
  struct list_elem elem;
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
    struct list allocated_blocks;
  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  int pointer_pos;
  disk_sector_t result;
  disk_sector_t target;

  if (pos < inode->data.length)
    // return inode->data.start + pos / DISK_SECTOR_SIZE;
    {
      if (pos < DIRECT_POINTER_REGION) 
      {
        pointer_pos = pos / DISK_SECTOR_SIZE;
        target = inode->data.direct_pointers[pointer_pos];
        if (target == NULL)
        {
          if (!free_map_allocate(1, &inode->data.direct_pointers[pointer_pos]))
            return -1;
          target = inode->data.direct_pointers[pointer_pos];
        }
        return target;
      }
      else if (pos < INDIRECT_POINTER_REGION)
      {
        struct indirect_block *ib = malloc(sizeof(struct indirect_block));
        if (ib == NULL) 
          result = -1;

        if (inode->data.indirect == NULL)
        {
          if (!free_map_allocate(1, &inode->data.indirect)) 
          {
            free(ib);
            return -1;
          }
        }

        if (fetch_cache(inode->data.indirect, ib, DISK_SECTOR_SIZE, 0))
        {
          pointer_pos = (pos - DIRECT_POINTER_REGION) / DISK_SECTOR_SIZE;
          target = ib->pointers[pointer_pos];
          if (target == NULL)
          {
            if (!free_map_allocate(1, &ib->pointers[pointer_pos])) 
            {
              free(ib);
              return -1;
            }
            target = ib->pointers[pointer_pos];
          }
          result = target;
        }
        else
          result = -1;

        free(ib);
        return result;
      }
      else if (pos < DOUBLY_INDIRECT_REGION) 
      {
        struct indirect_block *ib = malloc(sizeof(struct indirect_block));
        struct indirect_block *double_ib = malloc(sizeof(struct indirect_block));
        if (ib == NULL || double_ib == NULL) 
          result = -1;

        if (inode->data.doubly_indirect == NULL)
        {
          if (!free_map_allocate(1, &inode->data.doubly_indirect)) 
          {
            free(ib);
            free(double_ib);
            return -1;
          }
        }

        if (fetch_cache(inode->data.doubly_indirect, double_ib, DISK_SECTOR_SIZE, 0)) 
        {
          pointer_pos = (pos - INDIRECT_POINTER_REGION) / DISK_SECTOR_SIZE / INDIRECT_BLOCK_SIZE;
          if (double_ib->pointers[pointer_pos] == NULL)
          {
            if (!free_map_allocate(1, &double_ib->pointers[pointer_pos])) 
            {
              free(ib);
              free(double_ib);
              return -1;
            }
          }
          if (fetch_cache(double_ib->pointers[pointer_pos], ib, DISK_SECTOR_SIZE, 0))
          {
            pointer_pos = (pos - INDIRECT_POINTER_REGION) / DISK_SECTOR_SIZE - INDIRECT_BLOCK_SIZE * pointer_pos;
            target = ib->pointers[pointer_pos];
            if (target == NULL)
            {
              if (!free_map_allocate(1, &ib->pointers[pointer_pos]))
              {
                free(ib);
                free(double_ib);
                return -1;
              }
              target = ib->pointers[pointer_pos];
            }
            result = target;
          }
          else
            result = -1;
        }
        else
          result = -1;
        
        free(ib);
        free(double_ib);
        return result;

      }
    }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct list buffer_cache;

static struct lock buffer_lock;
static struct lock evict_lock;

static int buffer_cache_cnt = 0;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  list_init (&buffer_cache);
  lock_init (&buffer_lock);
  lock_init (&evict_lock);
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
      int i;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      for (i = 0; i < 12; i++)
        disk_inode->direct_pointers[i] = NULL;
      disk_inode->doubly_indirect = NULL;
      disk_inode->indirect = NULL;
      disk_write (filesys_disk, sector, disk_inode);
      // if (free_map_allocate (sectors, &disk_inode->start))
      //   {
      //     disk_write (filesys_disk, sector, disk_inode);
      //     if (sectors > 0) 
      //       {
      //         static char zeros[DISK_SECTOR_SIZE];
      //         size_t i;
              
      //         for (i = 0; i < sectors; i++) 
      //           disk_write (filesys_disk, disk_inode->start + i, zeros); 
      //       }
      //     success = true; 
      //   } 
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
  list_init(&inode->allocated_blocks);
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
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
          struct list_elem *e;
          struct allocated_blocks_entry *b;
          for (e = list_begin(&inode->allocated_blocks); e != list_end(&inode->allocated_blocks); e = list_next(e))
          {
            b = list_entry(e, struct allocated_blocks_entry, elem);
            free_map_release (b->idx, 1);
          }
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

  // lock_acquire (&buffer_lock);
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e))
  {
    c = list_entry(e, struct buffer_cache_entry, elem);
    if (c->idx == idx)
    {
      // lock_release (&buffer_lock);
      return c;
    }
  }

  lock_release (&buffer_lock);
  return NULL;
}

/* Fetch data from SECTOR_IDX of sector into buffer_cache_entry and
   push the entry into the buffer_cache_list. */
bool
fetch_sector (disk_sector_t idx)
{
  struct buffer_cache_entry *c = (struct buffer_cache_entry*) malloc(sizeof(struct buffer_cache_entry));

  if (check_cache(idx) != NULL)
    return false;

  lock_acquire (&buffer_lock);
  if (buffer_cache_cnt < BUFFER_CACHE_SIZE)
  {
    c->idx = idx;
    c->accessed = true;
    c->dirty = false;
    disk_read(filesys_disk, idx, c->data);
    list_push_back(&buffer_cache, &c->elem);
    buffer_cache_cnt++;
    
    lock_release (&buffer_lock);
    return true;
  }
  else
  {
    if (evict_sector (pick_entry_to_evict())) 
      return fetch_sector (idx);
    else
      return false;
  }
}

/* Thread function for fetching sector in background. */
bool
fetch_sector_background (disk_sector_t idx)
{
  thread_yield();
  return fetch_sector (idx);
}

/* Evicts sector from buffer_cache_list */
bool
evict_sector (disk_sector_t idx)
{
  struct list_elem *e;
  struct buffer_cache_entry *c;

  lock_acquire (&evict_lock);
  for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e))
  {
    c = list_entry(e, struct buffer_cache_entry, elem);
    if (c->idx == idx)
    {
      list_remove (e);
      disk_write (filesys_disk, c->idx, c->data);
      free (c);
      buffer_cache_cnt--;
      lock_release (&evict_lock);
      return true;
    }
  }

  lock_release (&evict_lock);
  return false;
}

/* Select which sector to evict from buffer cache. */
disk_sector_t
pick_entry_to_evict ()
{
  // Temporarily select random one
  int len = list_size (&buffer_cache);
  int random = timer_ticks() % len;

  struct list_elem *e;
  struct buffer_cache_entry *c;
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next(e))
  {
    if (! c->accessed)
    {
      return c->idx;
    }
    c->accessed = false;
  }
  
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next(e))
  {
    if (! c->accessed)
    {
      return c->idx;
    }
    c->accessed = false;
  }
}

/* Write all dirty-bit masked inodes. */
void 
write_dirty_inodes ()
{
  struct list_elem *e;
  struct buffer_cache_entry *c;
  lock_acquire (&buffer_lock);
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next(e))
  {
    c = list_entry (e, struct buffer_cache_entry, elem);
    if (c->dirty)
    {
      disk_write (filesys_disk, c->idx, c->data);
      c->dirty = false;
    }
  }
  lock_release (&buffer_lock);
}

/* Free buffer cache. */
void
free_buffer_cache ()
{
  struct list_elem *e;
  struct buffer_cache_entry *c;

  lock_acquire (&buffer_lock);
  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next (e))
  {
    c = list_entry (e, struct buffer_cache_entry, elem);
    list_remove (e);
    if (c->dirty)
    {
      disk_write (filesys_disk, c->idx, c->data);
    }
    free (c);
  }
  free (&buffer_cache);
}

/* Fetch SIZE bytes from cache into BUFFER from OFFSET.
   Returns the number of bytes acually read. */
bool
fetch_cache (disk_sector_t idx, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  struct buffer_cache_entry* cache_entry = check_cache(idx);

  if (cache_entry != NULL)
  {
    cache_entry->accessed = true;
    memcpy(buffer, cache_entry->data + offset, size);
    return true;
  }
  else
  {
    if (fetch_sector (idx))
    {
      return fetch_cache (idx, buffer, size, offset);
    }
    else 
    {
      return false;
    }
  }
}

/* Commit SIZE bytes into cache from BUFFER starting from OFFSET. 
   If there is no cache of SECTOR_IDX, fetch from disk. */
bool
commit_cache (disk_sector_t idx, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  struct buffer_cache_entry* cache_entry = check_cache (idx);
  
  if (cache_entry != NULL)
  {
    cache_entry->dirty = true;
    cache_entry->accessed = true;
    memcpy (cache_entry->data + offset, buffer, size);
    return true;
  }
  else
  {
    if (fetch_sector (idx))
    {
      return commit_cache (idx, buffer, size, offset);
    }
    else
    {
      return false;
    }
  }
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  disk_sector_t last_sector_used;

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

      if (!fetch_cache(sector_idx, buffer + bytes_read, chunk_size, sector_ofs))
        break;

      // if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
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
      //     disk_read (filesys_disk, sector_idx, bounce);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
      last_sector_used = sector_idx;
    }
  // free (bounce);

  thread_create ("read_ahead", thread_current()->priority - 1, fetch_sector_background, last_sector_used);

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
  disk_sector_t last_sector_used;

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

      if (!commit_cache (sector_idx, buffer + bytes_written, chunk_size, sector_ofs))
        break;

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
      last_sector_used = sector_idx;
    }
  // free (bounce);
  thread_create ("read_ahead", thread_current()->priority - 1, fetch_sector_background, last_sector_used);

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
