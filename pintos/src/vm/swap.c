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
static int swap_num = 0;
static int swap_in_num = 0;

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
swap_in (void *addr, struct sup_page_table_entry *page)
{
    // printf("trying swap in %d times.\n", ++swap_in_num);
    bool success = false;
    lock_acquire(&swap_lock);
    // struct sup_page_table_entry *page = find_page(thread_current()->supt, addr);
    ASSERT(page != NULL);
    if (page->loc != ON_SWAP)
    {
        lock_release(&swap_lock);
        printf("Not swapped\n");
        return false;
    }
    // struct frame_table_entry *frame = allocate_frame(PAL_USER, addr);
    success = read_from_disk(addr, page->swap_index);
    // success = success & frame_install_page(frame, addr);
    bitmap_set(swap_table, page->swap_index, false);
    lock_release(&swap_lock);
    page->swap_index = -1;
    page->loc = ON_FRAME;

    // printf("swap in %s\n", success ? "success" : "failed");
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
    // printf("trying swap out for %d times.\n", ++swap_num);
    // ++swap_num;
    // if (swap_num >= 517)
    // {
    //     printf("debug point\n");
    // }
    bool success = false;
    struct thread *curr = thread_current();
    struct frame_table_entry *frame;

    frame = select_frame_to_evict();
    ASSERT(frame != NULL);

    pagedir_clear_page(frame->owner->pagedir, frame->uaddr);

    size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
    success = write_to_disk(frame->frame, swap_index);
    struct sup_page_table_entry *page = find_page(frame->owner->supt, frame->uaddr);
    if (page == NULL)
    {
        printf("page null, frame uaddr: %p\n", frame->uaddr);
    }
    page->loc = ON_SWAP;
    page->swap_index = swap_index;
    // printf("swapped out to index of %d\n", swap_index);
    free_frame(frame->frame);

    // printf("swap out %s\n", success ? "success" : "failed");

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
        // printf("reading %p into %p\n", index * SECTOR_PER_PAGE + i, frame + (DISK_SECTOR_SIZE * i));
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

