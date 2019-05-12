#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "vm/page.h"
#include "lib/kernel/list.h"

struct frame_table_entry
{
	uint32_t* frame;
	struct thread* owner;
	struct sup_page_table_entry* spte;
	struct list_elem elem;
};

void frame_init (void);
struct frame_table_entry* allocate_frame (void *addr);
bool free_frame (struct frame_table_entry *frame, void *addr);
bool frame_install_page (struct frame_table_entry *frame, void *addr);

#endif /* vm/frame.h */
