#ifndef VM_SWAP_H
#define VM_SWAP_H
#include <stdbool.h>
#include <stdint.h>
#include "vm/page.h"

void swap_init (void);
bool swap_in (void *addr, struct sup_page_table_entry *page);
bool swap_out (void);
bool read_from_disk (uint8_t *frame, int index);
bool write_to_disk (uint8_t *frame, int index);
void free_swap(int index);

#endif /* vm/swap.h */
