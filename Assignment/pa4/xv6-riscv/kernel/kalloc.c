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
int num_free_pages;
int num_lru_pages;
struct spinlock lrulock; // lru lock
// pa4: bitmap
char *bitmap;
struct spinlock swaplock; // swaplock


void
kinit()
{
  initlock(&kmem.lock, "kmem");

  // pa4: initialize locks
  initlock(&swaplock, "swaplock");
  initlock(&lrulock, "lru");
  // initialize bitmap
  bitmap = end;
  memset(bitmap, 0, PGSIZE);
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// pa4: setting a swap slot in bitmap
// return: index of free block in swap space, -1 if fail
int
set_swapslot()
{
    // check the bits in swap space
    for(int i = 0; i < SWAPMAX; i++)
    {
        int byte = i / 8;
        int bit = i % 8;
        // find a free bit slot
        acquire(&swaplock);
        if(((bitmap[byte] >> bit) & 1) == 0)
        {
            // set the slot in bitmap
            bitmap[byte] |= (1 << bit);
            release(&swaplock);
            // return slot index
            return i;
        }
        release(&swaplock);
    }

    return -1;
}

// pa4: free swapslot without lock
void
free_swapslot_nolock(int slot)
{
    int byte = slot / 8;
    int bit = slot % 8;
    // clear the slot in bitmap
    bitmap[byte] &= ~(1 << bit);
}

// pa4: free the given swap slot
void
free_swapslot(int slot)
{
    acquire(&swaplock);
    free_swapslot_nolock(slot);
    release(&swaplock);
}

// pa4: lru add without lock
void
lru_add_nolock(struct page *p)
{
    // if lru is empty
    if(page_lru_head == 0)
    {
        // the page will be the head
        page_lru_head = p;
        p->next = p;
        p->prev = p;
    }
    else
    {
        // insert page at the tail
        // which is right before the head
        p->next = page_lru_head;
        p->prev = page_lru_head->prev;
        page_lru_head->prev->next = p;
        page_lru_head->prev = p;
    }
}

// pa4: add page to lru
void
lru_add(struct page *p)
{
    // call lru add with lock
    acquire(&lrulock);
    lru_add_nolock(p);
    release(&lrulock);
}

// pa4: remove page from lru without lock
void
lru_remove_nolock(struct page *p)
{
    // if page is the only page in the lru
    if(page_lru_head == p && p->next == p)
    {
        // clear the linked list
        page_lru_head = 0;
    }
    else
    {
        // remove page from list
        p->prev->next = p->next;
        p->next->prev = p->prev;
        if(page_lru_head == p)
            page_lru_head = p->next;
    }
}

// pa4: remove page from lru
void
lru_remove(struct page *p)
{
    acquire(&lrulock);
    lru_remove_nolock(p);
    release(&lrulock);
}

// pa4: swap out function
// return 0 if fail, 1 on success
int
swap_out(void)
{
    struct page *p;
    pte_t *pte;
    uint64 pa;
    int idx;

    acquire(&lrulock);

    // if lru is empty, return
    if(page_lru_head == 0)
    {
        release(&lrulock);
        return 0;
    }

    p = page_lru_head;
    // begin clock algorithm
    while(1)
    {
        //retrieve pte of page
        pte = walk(p->pagetable, (uint64)p->vaddr, 0);

        // if access bit is set
        if((*pte) & PTE_A)
        {
            // clear the bit to give a second chance
            *pte &= ~PTE_A;
            // move on to next page
            p = p->next;
            // update the head
            page_lru_head = p;
        }
        // else access bit is clear
        else
        {
            // found a victim, exit loop
            break;
        }
    }

    // calculate physical address of PTE
    pa = PTE2PA(*pte);
    // mark invalid
    *pte &= ~PTE_V;

    // remove from LRU list
    lru_remove_nolock(p);
    // release lru lock
    release(&lrulock);

    // now prepare a swap slot in the bitmap
    // if fail, return 0
    if((idx = set_swapslot()) == -1)
        return 0;
    
    // write into swap space
    swapwrite(pa, idx * 8);
    
    // update PTE with swap flag and swap index
    *pte = ((uint64)idx << 10) | PTE_S | PTE_FLAGS(*pte);
    // clear valid and access
    *pte &= ~PTE_V;
    *pte &= ~PTE_A;

    // flush TLB
    sfence_vma();
    
    // free pa
    kfree((void*)pa);

    return 1;
}
