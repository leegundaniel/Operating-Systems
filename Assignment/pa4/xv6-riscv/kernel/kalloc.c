// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void freepagespace(void);
void* kalloc(void);
int swap_out(void);
int swap_in(pagetable_t, uint64);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// pa4: struct for page control
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
struct page temp;
int num_free_pages = 0;
int num_lru_pages;
unsigned char *bitmap;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  num_free_pages++;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// pa4: kalloc function
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  
  // if page not allocated
  if(!r)
  {
    // attempt to swap out
    if(!swap_out())
    {
        // if swap out failed, print error message and return 0
        printf("Error: OOM\n");
        return 0;
    }
  }

  // attempt to allocate again
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    num_free_pages--;
  }
  return (void*)r;
}

// pa4: swapinit
// initialize LRU and bitmap
void
swapinit(void)
{
  // initialize lru values
  page_lru_head = &temp;
  page_lru_head->prev = page_lru_head;
  page_lru_head->next = page_lru_head;
  num_lru_pages = 0;
  
  // set page for bitmap
  bitmap = kalloc();
  if(bitmap == 0)
      panic("bitmap: kalloc failed");
  // clear bitmap
  memset(bitmap, 0, PGSIZE);
}

// pa4: add page to lru
// increment number of lru pages
void
lru_add(struct page *page)
{
    // connect the page to the end of the lru list
    page->prev = page_lru_head->prev;
    page->next = page_lru_head;

    page_lru_head->prev->next = page;
    page_lru_head->prev = page;
    
    // increment number of lru pages
    num_lru_pages++;
}

// pa4: remove page from lru
// decrement number of lru pages
void
lru_remove(struct page *page)
{
    if(num_lru_pages == 0)
    {
        return;
    }
    // unlink page from the lru list
    page->prev->next = page->next;
    page->next->prev = page->prev;
    // cleanup pointer
    page->next = 0;
    page->prev = 0;

    // decrement number of pages in LRU list
    num_lru_pages--;
}

// pa4: choose victim page using clock algorithm
// return the victim page on success
// and 0 on fail
struct page*
lru_victim(void)
{
    printf("LRU VICTIM\n");
    struct page* page = page_lru_head->next;
    pte_t *pte = 0;

    // if there are no lru pages, return NULL
    if(num_lru_pages == 0)
        return 0;

    // loop through the lru pages
    for(int i = 0; i < num_lru_pages; i++)
    {
        // locate pte of current page
        pte = walk(page->pagetable, (uint64)page->vaddr, 0);
       
        // if PTE_A is set
        if(*pte & PTE_A)
        {
            // clear PTE_A
            *pte &= ~PTE_A;

            // send page to the tail of the list (right before the head)
            struct page* p = page->next;
            lru_remove(page);
            lru_add(page);
            // next page
            page = p;
            continue;
        }
        // if PTE_A == 0
        else
        {
            // found victim page
            return page;
        }
    }
    // no victim page found
    return 0;
}

// pa4: set bit
// finds a free bit in the bitmap and sets it
// return: the "bit" / slot that was set on success
//         -1 on fail
int
set_bit(void)
{
    for(int i = 0; i < SWAPMAX; i++)
    {
        int byte = i / 8;
        int bit = i % 16;

        // check if bit slot is free
        if((bitmap[byte] & (1 << bit)) == 0)
        {
            // set bit
            bitmap[byte] |= (1 << bit);
            return i;
        }
    }
    // bitmap is full
    return -1;
}

// pa4: clear bit
// finds slot in the bitmap and clears bit
// return: 1 on success; 0 on fail
int
clear_bit(int slot)
{
    if(slot < 0 || slot >= SWAPMAX)
        return 0;
    
    int byte = slot / 8;
    int bit = slot % 8;

    bitmap[byte] &= ~(1 << bit);

    return 1;
}

// pa4: swap out
// return 1 on success, 0 on fail
int
swap_out(void)
{
    printf("SWAPOUT\n");
    struct page *page = lru_victim();
    pte_t *pte;
   printf("free pages = %d\n", num_free_pages); 
    if(page == 0)
        return 0;
    
    pte = walk(page->pagetable, (uint64)page->vaddr, 0);
    if(pte == 0 || (*pte & PTE_V) == 0)
        return 0;

    uint64 pa = PTE2PA(*pte);
    uint64 flags = PTE_FLAGS(*pte);

    int slot = set_bit();
    if(slot < 0)
        return 0;
    int blkno = SWAPBASE + slot;
    printf("SWAP OUT: victim pa = %ld, slot = %d\n", pa, slot);
    swapwrite(pa, blkno);
    kfree((void *)pa);
    printf("free pages = %d\n", num_free_pages);


    // Modify PTE
    flags &= ~PTE_V;
    *pte = SLOT2PTE(slot, flags);
    
    lru_remove(page);
    
    return 1;
}

// pa4: swap in
// return 1 on success, 0 on fail
int
swap_in(pagetable_t pagetable, uint64 va)
{
    printf("SWAPIN\n");
    pte_t *pte = walk(pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 1)
        return 0;

    uint64 flags = PTE_FLAGS(*pte);
    int slot = PTE2SLOT(*pte);

    if(slot < 0 || slot >= SWAPMAX)
        return 0;

    char *mem = kalloc();
    if(mem == 0)
    {
        if(!swap_out())
            return 0;

        mem = kalloc();

        if(mem == 0)
            return 0;
    }
    swapread((uint64)mem, SWAPBASE + slot);

   *pte = PA2PTE(mem) | flags | PTE_V;

   clear_bit(slot);

   struct page *page = &pages[(mem - (char*)KERNBASE) / PGSIZE];
   page->pagetable = pagetable;
   page->vaddr = mem;
   lru_add(page);

    return 1;
}
