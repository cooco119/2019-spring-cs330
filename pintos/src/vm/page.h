#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <stdbool.h>
#include <stdint.h>
#include "lib/kernel/list.h"
#include <hash.h>
#include <debug.h>
#include "vm/frame.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

enum page_location
{
	ON_FRAME,
	ON_SWAP,
	NONE,
	ON_FILE
};

enum page_state
{
	ACTIVE,
	INACTIVE
};

struct sup_page_table_entry 
{
	uint32_t* user_vaddr;
	uint64_t access_time;

	bool dirty;
	bool accessed;
	enum page_location loc;
	struct frame_table_entry *frame;
	bool active;

	int swap_index;

	struct list_elem elem;
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	bool writable;
};

struct list *page_init (void);
struct sup_page_table_entry *allocate_page (void *addr);
struct sup_page_table_entry* find_page(struct list supt, void *addr);
bool load_page(void *addr, uint32_t *pd);
void free_page(struct list *supt, void *addr);
void free_page_table (void);
bool grow_stack(struct list supt, void *page);
bool install_from_file (struct list *supt, void *uaddr, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool page_unmap(struct list supt, uint32_t *pd, void* addr, struct file *f, off_t ofs);
#endif /* vm/page.h */
