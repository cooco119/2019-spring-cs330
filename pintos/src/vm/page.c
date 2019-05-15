#include "vm/page.h"
#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "threads/palloc.h"

struct list *supt_list;

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

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
    if (page == NULL){
        printf("page allocation failed\n");
        return NULL;
    }
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
find_page(struct list supt, void *addr)
{
    struct sup_page_table_entry *page;
    struct list_elem *e;
    // addr = pg_round_down(addr);
    // printf("finding page in %p\n", addr);

    if (list_empty(&supt)) return NULL;
    for(e = list_front(&supt); e != list_end(&supt); e = list_next(e))
    {
        page = list_entry(e, struct sup_page_table_entry, elem);
        // if (addr > 0x8240000)
        // {
        //     printf("page addr : %p, uaddr: %p\n", page->user_vaddr, addr);
        // }
        if (page->user_vaddr == addr)
        {
            return page;
        }
    }
    printf("page not found : %p, user addr? : %s\n", addr, is_user_vaddr(addr) ? "true": "false");
    return NULL;
}

bool
load_page(void *addr, uint32_t *pd)
{
    bool writable = true;
    struct sup_page_table_entry *page = find_page(thread_current()->supt, addr);
    if (page != NULL)
    {
        struct frame_table_entry *frame = allocate_frame(PAL_USER, addr);
        frame->spte = page;
        page->frame = frame->frame;
        if (frame == NULL){
            printf("frame alloc failed\n");
            return false;
        }
        
        switch (page->loc)
        {
        case ON_FRAME:
            break;
        
        case ON_SWAP:
            // printf("useraddr : %p, kernel addr: %p\n", addr, frame->frame);
            swap_in(frame->frame, page);
            break;
        
        case NONE:
            memset (frame->frame, 0, PGSIZE);
            break;

        case ON_FILE:
            file_seek(page->file, page->ofs);
            uint32_t read = file_read(page->file, frame->frame, page->read_bytes);
            if (read != page->read_bytes)
            {
                free_frame(frame->frame);
                return false;
            }
            memset (frame->frame + read, 0, page->zero_bytes);
            writable = page->writable;
            // if (!install_page (addr, frame->frame, writable)) 
            //   {
            //     free_frame (frame->frame);
            //     return false; 
            //   }
            // if (!frame_install_page(frame, upage))
      // {
      //   printf("frame install failed\n");
      //   free_frame (kpage);
      //   return false;
      // }
            break;

        default:
            break;
        }

        if (!pagedir_set_page(pd, addr, frame->frame, writable))
        {
            free_frame(frame->frame);
            printf("pagedir setting fail\n");
            return false;
        }

        page->loc = ON_FILE;
        page->active = true;
        pagedir_set_dirty(pd, frame->frame, false);
        page->access_time = timer_ticks();

        // printf("loading page success\n");
        return true;
    }
    else{
        printf("page not found\n");
        return false;
    }
}

void
free_page (struct list *supt, void *addr)
{
    // printf("freeing page\n");
    struct sup_page_table_entry *page;
    struct list_elem *e;

    if (!list_empty(supt))
    {
        lock_acquire(&thread_current()->supt_lock);
        for (e = list_front(supt); e != list_back(supt); e = list_next(e))
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
                free_frame(page->frame->frame);
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
    new_page->loc = NONE;

    lock_acquire(&thread_current()->supt_lock);
    list_push_back(&supt, &new_page->elem);
    success = true;
    lock_release(&thread_current()->supt_lock);

    return success;
}

bool
install_from_file (struct list *supt, void *uaddr, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
    struct sup_page_table_entry *page = (struct sup_page_table_entry *) malloc (sizeof (struct sup_page_table_entry));

    page->user_vaddr = uaddr;
    page->loc = ON_FILE;
    page->file = file;
    page->ofs = ofs;
    page->read_bytes = read_bytes;
    page->zero_bytes = zero_bytes;
    page->writable = writable;

    lock_acquire(&thread_current()->supt_lock);
    list_push_back(supt, &page->elem);
    lock_release(&thread_current()->supt_lock);

    // printf("installed from file, uaddr: %p\n", uaddr);
    return true;
}

bool
page_unmap(struct list supt, uint32_t *pd, void* addr, struct file *f, off_t ofs)
{
    file_seek(f, ofs);
    struct sup_page_table_entry *page = find_page(supt, addr);
    if (page == NULL)
    {
        PANIC("unmap failed");
    }
    bool dirty;
    switch (page->loc)
    {
        case ON_FRAME:
            dirty = false;
            dirty = dirty || pagedir_is_dirty(pd, page->user_vaddr);
            dirty = dirty || pagedir_is_dirty(pd, page->frame);
            if (dirty)
            {
                file_write_at(f, page->user_vaddr, PGSIZE, ofs);
            }
            free_frame(page->frame);
            pagedir_clear_page(pd, page->user_vaddr);
            break;
        case ON_SWAP:
            free_swap(page->swap_index);
            break;
        case ON_FILE:
            break;
        default:
            break;
    }

    lock_acquire(&thread_current()->supt_lock);
    list_remove(&page->elem);
    lock_release(&thread_current()->supt_lock);

    return true;
}