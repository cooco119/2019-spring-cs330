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
bool
allocate_frame (void *addr)
{
    struct frame_table_entry *frame = (struct frame_table_entry *) malloc(sizeof(struct frame_table_entry));
    struct sup_page_table_entry *page = (struct sup_page_table_entry *) malloc(sizeof(struct sup_page_table_entry));
    if (frame == NULL)
    {
        return false;
    }
    frame->frame = &frame_table;
    frame->owner = thread_current();
    frame->spte = page;
    if (frame->spte == NULL)
    {
        return false;
    }
    frame->spte = allocate_page(addr);

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &frame->elem);
    lock_release(&frame_lock);

    return true;
}

bool
free_frame (void *addr)
{
    struct frame_table_entry *frame;
    struct list_elem *e;
    
    lock_acquire(&frame_lock);
    for (e = list_front(&frame_table); e != list_back(&frame_table); e = list_next(e))
    {
        frame = list_entry(e, struct frame_table_entry, elem);
        if (frame->spte->user_vaddr == addr)
        {
            palloc_free_page(addr);
            list_remove(e);
            return;
        }
    }
    lock_release(&frame_lock);
}