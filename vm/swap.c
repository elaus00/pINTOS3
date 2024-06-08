#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"

// The block device used for swap space
static struct block *swap_block;

// Bitmap to track available swap slots
static struct bitmap *swap_available;

// Number of sectors per page
static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

// Total number of swap slots
static size_t swap_size;

// Initialize the swap system
void vm_swap_init() {
  ASSERT(SECTORS_PER_PAGE > 0);
  
  // Retrieve the swap block
  swap_block = block_get_role(BLOCK_SWAP);
  
  // Check if swap_block is successfully obtained
  if (swap_block == NULL) {
    PANIC("Error: Can't initialize swap block");
    NOT_REACHED();
  }
  
  // Calculate the total number of swap slots
  swap_size = block_size(swap_block) / SECTORS_PER_PAGE;
  
  // Create and initialize the bitmap to track available swap slots
  swap_available = bitmap_create(swap_size);
  bitmap_set_all(swap_available, true);
}

// Swap out a page to the swap space
swap_index_t vm_swap_out(void *page) {
  ASSERT(page >= PHYS_BASE);
  
  // Find an available swap slot
  size_t swap_index = bitmap_scan(swap_available, 0, 1, true);
  
  // Write the contents of the page to the swap space
  size_t i;
  for (i = 0; i < SECTORS_PER_PAGE; ++i) {
    block_write(swap_block, swap_index * SECTORS_PER_PAGE + i, page + (BLOCK_SECTOR_SIZE * i));
  }
  
  // Mark the swap slot as occupied
  bitmap_set(swap_available, swap_index, false);
  
  return swap_index;
}

// Swap in a page from the swap space
void vm_swap_in(swap_index_t swap_index, void *page) {
  ASSERT(page >= PHYS_BASE);
  ASSERT(swap_index < swap_size);
  
  // Check if the swap slot is assigned
  if (bitmap_test(swap_available, swap_index) == true) {
    PANIC("Error, invalid read access to unassigned swap block");
  }
  
  // Read the contents of the swap slot into the page
  size_t i;
  for (i = 0; i < SECTORS_PER_PAGE; ++i) {
    block_read(swap_block, swap_index * SECTORS_PER_PAGE + i, page + (BLOCK_SECTOR_SIZE * i));
  }
  
  // Mark the swap slot as available
  bitmap_set(swap_available, swap_index, true);
}

// Free a swap slot
void vm_swap_free(swap_index_t swap_index) {
  ASSERT(swap_index < swap_size);
  if (!bitmap_test(swap_available, swap_index)) {
    PANIC("Error, invalid free request to unassigned swap block");
  }
  bitmap_set(swap_available, swap_index, true);
}