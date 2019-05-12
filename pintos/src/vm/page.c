#include "vm/page.h"
#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "devices/timer.h"

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
    page->kernel_addr = vtop(addr);
    page->access_time = timer_ticks();
    page->state = ON_FRAME;

    lock_acquire(&curr->supt_lock);
    list_push_back(&curr->supt, &page->elem);
    lock_release(&curr->supt_lock);

    return page;
}

void
free_page (struct list *supt, void *addr)
{
}


