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
static int page_num = 0;

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
    struct frame_table_entry *f;
    struct list_elem *e;
    uint32_t *addr;

    addr = palloc_get_page (PAL_USER | flags);
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
    frame->spte = "NULL";
    frame->hold = true;

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
        frame = list_entry(e, struct frame_table_entry, elem);
        // if (frame->hold) continue;
        // printf("iterating frame of uaddr : %p\n", frame->uaddr);
        if (! pagedir_is_accessed(curr->pagedir, frame->uaddr))
        {
            // printf("evicting frame of uaddr : %p\n", frame->uaddr);
            return frame;
        }
        pagedir_set_accessed(curr->pagedir, frame->uaddr, false);
    }

    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        frame = list_entry(e, struct frame_table_entry, elem);
        // if (frame->hold) continue;
        if (! pagedir_is_accessed(curr->pagedir, frame->uaddr))
        {
            // printf("evicting frame of uaddr : %p\n", frame->uaddr);
            return frame;
        }
        pagedir_set_accessed(curr->pagedir, frame->uaddr, false);
    }

    // static unsigned tmp = 1;
    // tmp = tmp * 1928471u + 1029381723u;
    // size_t pointer = tmp % list_size(&frame_table);
    // int i = 0; e = list_begin(&frame_table);
    // for (i = 0; i < pointer; i++)
    // {
    //     e = list_next(e);
    // }
    // return frame = list_entry(e, struct frame_table_entry, elem);

    return NULL;
}


bool
free_frame (void *addr)
{
    // printf("freeing frame\n");
    struct frame_table_entry *frame;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        frame = list_entry(e, struct frame_table_entry, elem);
        if (frame->frame == addr){
            list_remove(&frame->elem);
            free(frame);
            break;
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
    // printf("installed spte in %p. total installed pages: %d\n", addr, ++page_num);
    // if (page_num >= 517)
    // {
    //     printf("for debug\n");
    // }
    // printf("alloc page in %p\n", addr);
    frame->spte = page;
    frame->hold = false;

    return true;
}