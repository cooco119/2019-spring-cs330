#include "vm/swap.h"
#include "devices/disk.h"
#include "threads/synch.h"
#include <bitmap.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

static const size_t SECTOR_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;
static size_t swap_size;

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
static struct bitmap *swap_table;

/* Protects swap_table */
static struct lock swap_lock;

/* 
 * Initialize swap_device, swap_table, and swap_lock.
 */
void 
swap_init (void)
{
    swap_device = disk_get(1, 1);
    swap_size = disk_size(swap_device) / SECTOR_PER_PAGE;
    swap_table = bitmap_create(swap_size);
    lock_init(&swap_lock);
}

/*
 * Reclaim a frame from swap device.
 * 1. Check that the page has been already evicted. 
 * 2. You will want to evict an already existing frame
 * to make space to read from the disk to cache. 
 * 3. Re-link the new frame with the corresponding supplementary
 * page table entry. 
 * 4. Do NOT create a new supplementray page table entry. Use the 
 * already existing one. 
 * 5. Use helper function read_from_disk in order to read the contents
 * of the disk into the frame. 
 */ 
bool 
swap_in (void *addr)
{
    bool success = false;
    lock_acquire(&swap_lock);
    struct sup_page_table_entry *page = find_page(addr);
    if (page->loc != ON_SWAP)
    {
        lock_release(&swap_lock);
        return false;
    }

    struct frame_table_entry *frame = allocate_frame(PAL_USER, addr);
    success = read_from_disk(frame->frame, page->swap_index);
    success = success & frame_install_page(frame, addr);
    lock_release(&swap_lock);
    page->swap_index = -1;
    page->loc = ON_FRAME;

    return success;
}

/* 
 * Evict a frame to swap device. 
 * 1. Choose the frame you want to evict. 
 * (Ex. Least Recently Used policy -> Compare the timestamps when each 
 * frame is last accessed)
 * 2. Evict the frame. Unlink the frame from the supplementray page table entry
 * Remove the frame from the frame table after freeing the frame with
 * pagedir_clear_page. 
 * 3. Do NOT delete the supplementary page table entry. The process
 * should have the illusion that they still have the page allocated to
 * them. 
 * 4. Find a free block to write you data. Use swap table to get track
 * of in-use and free swap slots.
 */
bool
swap_out (void)
{
    bool success = false;
    struct thread *curr = thread_current();
    struct frame_table_entry *frame;

    frame = select_frame_to_evict();

    struct sup_page_table_entry *page = frame->spte;
    pagedir_clear_page(curr->pagedir, frame->frame);
    free_frame(frame->frame);
    page->loc = ON_SWAP;

    size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
    success = write_to_disk(frame->frame, swap_index);
    page->swap_index = swap_index;

    return success;
}

/* 
 * Read data from swap device to frame. 
 * Look at device/disk.c
 */
bool read_from_disk (uint8_t *frame, int index)
{
    size_t i;
    for (i = 0; i < SECTOR_PER_PAGE; i++)
    {
        disk_read(swap_device, index * SECTOR_PER_PAGE + i, frame + (DISK_SECTOR_SIZE * i));
    }
    return true;
}

/* Write data to swap device from frame */
bool write_to_disk (uint8_t *frame, int index)
{
    size_t i;
    for (i = 0; i < SECTOR_PER_PAGE; i++)
    {
        disk_write(swap_device, index * SECTOR_PER_PAGE + i, frame + (DISK_SECTOR_SIZE * i));
    }
    return true;
}

