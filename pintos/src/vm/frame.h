#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "vm/page.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"

struct frame_table_entry
{
	uint32_t* frame;
	uint32_t* uaddr;
	struct thread* owner;
	struct sup_page_table_entry* spte;
	struct list_elem elem;
	bool hold;
};

void frame_init (void);
struct frame_table_entry* allocate_frame (enum palloc_flags flags, void *upage);
struct frame_table_entry* select_frame_to_evict(void);
bool free_frame (void *addr);
bool frame_install_page (struct frame_table_entry *frame, void *addr);

#endif /* vm/frame.h */
