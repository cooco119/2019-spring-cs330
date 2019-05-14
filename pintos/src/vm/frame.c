#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"

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
allocate_frame (enum palloc_flags flags, void *upage)
{
    uint8_t *addr = palloc_get_page (PAL_USER | flags);
    if (addr == NULL){
        swap_out();
        addr = palloc_get_page(PAL_USER | flags);
        ASSERT (addr != NULL);
    }
    struct frame_table_entry *frame = (struct frame_table_entry *) malloc(sizeof(struct frame_table_entry));
    if (frame == NULL)
    {
        return NULL;
    }
    frame->frame = addr;
    frame->uaddr = upage;
    frame->owner = thread_current();
    frame->spte = NULL;

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &frame->elem);
    lock_release(&frame_lock);

    return frame;
}

struct frame_table_entry*
select_frame_to_evict(void)
{
    struct thread *curr = thread_current();
    struct frame_table_entry* frame;
    struct list_elem *e;

    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        if (! pagedir_is_accessed(curr->pagedir, frame->uaddr))
        {
            return frame;
        }
        pagedir_set_accessed(curr->pagedir, frame->uaddr, false);
    }

    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        if (! pagedir_is_accessed(curr->pagedir, frame->uaddr))
        {
            return frame;
        }
        pagedir_set_accessed(curr->pagedir, frame->uaddr, false);
    }

    return NULL;
}


bool
free_frame (void *addr)
{
    struct frame_table_entry *frame;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        frame = list_entry(e, struct frame_table_entry, elem);
        if (frame->frame == addr){
            list_remove(&frame->elem);
            free(frame);
        }
    }
    lock_release(&frame_lock);
    palloc_free_page(addr);
}

bool
frame_install_page (struct frame_table_entry *frame, void *addr)
{
    struct sup_page_table_entry *page = allocate_page(addr);
    if (page == NULL)
        return false;
    // printf("installed spte in %p\n", addr);
    frame->spte = page;

    return true;
}