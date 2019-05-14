#include "vm/page.h"
#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "userprog/pagedir.h"

struct list *supt_list;

/*
 * Initialize supplementary page table
 */
struct list *
page_init (void)
{
    list_init(supt_list);

    return supt_list;
}

/*
 * Make new supplementary page table entry for addr 
 */
struct sup_page_table_entry *
allocate_page (void *addr)
{
    struct sup_page_table_entry *page = (struct sup_page_table_entry *) malloc(sizeof(struct sup_page_table_entry));
    struct thread *curr = thread_current();

    page->user_vaddr = addr;
    page->access_time = timer_ticks();
    page->loc = ON_FRAME;
    page->active = true;

    struct sup_page_table_entry *p;
    struct list_elem *e;

    // if (!list_empty(&curr->supt)){
    //     for (e = list_front(&curr->supt); e = list_back(&curr->supt); e = list_next(e))
    //     {
    //         p = list_entry(e, struct sup_page_table_entry, elem);
    //         if (p->user_vaddr == addr)
    //         {
    //             free(page);
    //             return NULL;
    //         }
    //     }
    // }

    lock_acquire(&curr->supt_lock);
    list_push_back(&curr->supt, &page->elem);
    lock_release(&curr->supt_lock);

    return page;
}

struct sup_page_table_entry *
find_page(void *addr)
{
    struct thread *curr = thread_current();
    struct sup_page_table_entry *page;
    struct list_elem *e;

    for(e = list_front(&curr->supt); e != list_back(&curr->supt); e = list_next(e))
    {
        page = list_entry(e, struct sup_page_table_entry, elem);
        if (page->user_vaddr == addr)
        {
            return page;
        }
    }
    return NULL;
}

bool
load_page(void *addr, uint32_t *pd)
{
    struct sup_page_table_entry *page = find_page(addr);
    if (page != NULL)
    {
        struct frame_table_entry *frame = allocate_frame(addr);
        frame->spte = page;
        if (frame == NULL){
            return false;
        }
        
        switch (page->loc)
        {
        case ON_FRAME:
            page->frame = frame;
            break;
        
        case ON_SWAP:
            break;
        
        case NONE:
            break;

        default:
            break;
        }

        if (!pagedir_set_page(pd, addr, frame, true))
        {
            free_frame(addr, (void *) frame);
            return false;
        }

        page->loc = ON_FRAME;
        page->active = true;
        pagedir_set_dirty(pd, addr, false);

        return true;
    }
    else
        return false;
}

void
free_page (struct list *supt, void *addr)
{
    struct sup_page_table_entry *page;
    struct list_elem *e;

    if (!list_empty(supt))
    {
        lock_acquire(&thread_current()->supt_lock);
        for (e = list_front(supt); e != list_back(supt); e = list_next(supt))
        {
            page = list_entry(e, struct sup_page_table_entry, elem);
            if (page->user_vaddr == addr)
            {
                free(page);
            }
        }
        lock_release(&thread_current()->supt_lock);
    }
}

void
free_page_table (void)
{
    struct sup_page_table_entry *page;
    struct list_elem *e;

    struct list supt = thread_current()->supt;

    if (!list_empty(&supt))
    {
        lock_acquire(&thread_current()->supt_lock);
        for (e = list_front(&supt); e != list_back(&supt); e = list_next(e))
        {
            page = list_entry(e, struct sup_page_table_entry, elem);
            if (page->loc == ON_FRAME)
            {
                free_frame(page->frame, page->user_vaddr);
            }
            if (page->loc == ON_SWAP)
            {

            }

            free(page);
        }
        lock_release(&thread_current()->supt_lock);
    }

    free(&supt);
}

bool
grow_stack(struct list supt, void *page)
{
    bool success = false;
    struct sup_page_table_entry *new_page = (struct sup_page_table_entry*) malloc(sizeof(struct sup_page_table_entry));

    new_page->user_vaddr = page;
    new_page->active = true;
    new_page->loc = ON_FRAME;

    lock_acquire(&thread_current()->supt_lock);
    list_push_back(&supt, &new_page->elem);
    success = true;
    lock_release(&thread_current()->supt_lock);

    return success;
}

