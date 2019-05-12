#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

static struct lock frame_lock;
static struct list frame_table;

/*
 * Initialize frame table
 */
void 
frame_init (void)
{
    lock_init(&frame_lock);
    list_init(&frame_table);
}


/* 
 * Make a new frame table entry for addr.
 */
struct frame_table_entry*
allocate_frame (void *addr)
{
    struct frame_table_entry *frame = (struct frame_table_entry *) malloc(sizeof(struct frame_table_entry));
    if (frame == NULL)
    {
        return NULL;
    }
    frame->frame = &frame_table;
    frame->owner = thread_current();
    frame->spte = NULL;

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &frame->elem);
    lock_release(&frame_lock);

    return frame;
}

bool
free_frame (struct frame_table_entry *frame, void *addr)
{
    lock_acquire(&frame_lock);
    palloc_free_page(addr);
    list_remove(&frame->elem);
    free(frame);
    lock_release(&frame_lock);
}

bool
frame_install_page (struct frame_table_entry *frame, void *addr)
{
    struct sup_page_table_entry *page = allocate_page(addr);
    if (page == NULL)
        return false;

    frame->spte = page;

    return true;
}