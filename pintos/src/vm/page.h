#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <stdbool.h>
#include <stdint.h>
#include "lib/kernel/list.h"
#include <hash.h>
#include <debug.h>

enum page_state
{
	ON_FRAME,
	ON_SWAP,
	NONE
};

struct sup_page_table_entry 
{
	uint32_t* user_vaddr;
	uint32_t* kernel_addr;
	uint64_t access_time;

	bool dirty;
	bool accessed;
	enum page_state state;

	struct list_elem elem;
};

struct list *page_init (void);
struct sup_page_table_entry *allocate_page (void *addr);
void free_page(struct list *supt, void *addr);


#endif /* vm/page.h */
