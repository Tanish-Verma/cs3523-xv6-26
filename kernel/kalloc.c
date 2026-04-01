// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
// defined by kernel.ld.

int clock_hand = 0;
int active_frames = 0;

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct frame_entry frameTable[MAX_NFRAME];
struct swap_entry swapTable[NSWAPFRAMES];
struct spinlock frame_lock;
struct spinlock swap_lock;

void map_swapspace_pa_direct(void)
{
  uint64 pa = USABLE_PHYSTOP;
  for (int i = 0; i < NSWAPFRAMES; i++, pa += PGSIZE)
  {
    swapTable[i].pa = (void *)pa;
    memset((void *)pa, 0, PGSIZE); // fill with 0
  }
}

void initswapspace()
{
  initlock(&swap_lock, "swap_table_lock");
  map_swapspace_pa_direct();
  for (int i = 0; i < NSWAPFRAMES; i++)
  {
    swapTable[i].in_use = 0;
    swapTable[i].pagetable = 0;
    swapTable[i].va = 0;
  }
  //// debug print
  // printf("[SwapInit] frames=%d first_pa=%p last_pa=%p usable_top=%p phystop=%p\n",
  //        NSWAPFRAMES,
  //        swapTable[0].pa,
  //        swapTable[NSWAPFRAMES - 1].pa,
  //        (void *)USABLE_PHYSTOP,
  //        (void *)PHYSTOP);
}
void swap_in(uint64 va, pagetable_t pagetable, void *new_pa)
{
  acquire(&swap_lock);

  int found = 0;

  for (int i = 0; i < NSWAPFRAMES; i++)
  {
    if (swapTable[i].in_use == 1 && swapTable[i].pagetable == pagetable && swapTable[i].va == va)
    {

      memmove(new_pa, swapTable[i].pa, PGSIZE);

      swapTable[i].in_use = 0;
      swapTable[i].pagetable = 0;
      swapTable[i].va = 0;
      memset(swapTable[i].pa, 0, PGSIZE);
      pte_t *pte = walk(pagetable, va, 0);
      if (pte == 0)
        panic("swap_in: pte missing");

      int flags = PTE_FLAGS(*pte);

      *pte = PA2PTE(new_pa) | (flags & ~PTE_S) | PTE_V;
      found = 1;
      break;
    }
  }

  release(&swap_lock);

  if (!found)
  {
    printf("swap_in failed: page not found in swap space! va=%ld\n", va);
    panic("swap_in: Page not found in swap space!");
  }
}

int swap_out(uint64 va, pagetable_t pagetable, void *pa_to_evict)
{
  acquire(&swap_lock);

  int found = 0;

  for (int i = 0; i < NSWAPFRAMES; i++)
  {

    if (swapTable[i].in_use == 0)
    {
      swapTable[i].in_use = 1;
      swapTable[i].pagetable = pagetable;
      swapTable[i].va = va;

      // Copy the 4096 bytes from RAM into the reserved swap region
      memmove(swapTable[i].pa, pa_to_evict, PGSIZE);

      pte_t *pte = walk(pagetable, va, 0);
      if (pte == 0)
        panic("swap_out: pte missing");

      // Clear Valid bit, Set Swap bit. Leave all other permission flags alone
      *pte = (*pte & ~PTE_V) | PTE_S;
      found = 1;
      break;
    }
  }
  release(&swap_lock);
  if (!found)
  {
    // return -1 to indicate that the swap out failed due to no free swap slots
    //  The process will be killed by the caller when it sees the -1 return value
    return -1;
  }
  return 0;
}

void swap_free(uint64 va, pagetable_t pagetable)
{
  acquire(&swap_lock);

  for (int i = 0; i < NSWAPFRAMES; i++)
  {
    // Find the exact swap slot for this pagetable and virtual address
    if (swapTable[i].in_use == 1 && swapTable[i].pagetable == pagetable && swapTable[i].va == va)
    {

      // Clear the metadata
      swapTable[i].in_use = 0;
      swapTable[i].pagetable = 0;
      swapTable[i].va = 0;
      memset(swapTable[i].pa, 0, PGSIZE);
      break;
    }
  }

  release(&swap_lock);
}

void initframeTable()
{
  initlock(&frame_lock, "frame_table_lock");
  for (int i = 0; i < MAX_NFRAME; i++)
  {
    frameTable[i].in_use = 0;
    frameTable[i].proc = 0;
    frameTable[i].va = 0;
  }
}

void fillframeTable(void *pa, struct proc *p, uint64 va)
{
  acquire(&frame_lock);
  int found = 0;
  for (int i = 0; i < MAX_NFRAME; i++)
  {
    if (frameTable[i].in_use == 0)
    {
      frameTable[i].in_use = 1;
      frameTable[i].proc = p;
      frameTable[i].va = va;
      frameTable[i].pa = pa;
      //  //debug print
      //  if (p && p->pid > 2) {
      //    // printf("[FrameTracker] Added: PID %d, VA %ld, PA %p\n", p->pid, va, pa);
      //  }

      p->resident_pages++;

      found = 1;
      break;
    }
  }
  release(&frame_lock);
  if (!found)
    panic("frame table full");
}

void freeframeTable(void *pa)
{
  acquire(&frame_lock);
  for (int i = 0; i < MAX_NFRAME; i++)
  {
    if (frameTable[i].pa == pa)
    {
      frameTable[i].in_use = 0;
      struct proc *p = frameTable[i].proc;
      // printf("[FrameTracker] Freed PID:  %d\n", p ? p->pid : -1);
      frameTable[i].proc = 0;
      frameTable[i].va = 0;
      frameTable[i].pa = 0;
      if (p)
      {
        p->resident_pages--;
      }
      active_frames--;
      break;
    }
  }
  release(&frame_lock);
}

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)USABLE_PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  if (!r)
  {
    void *stolen_pa = evict_page();

    if (stolen_pa == 0)
    {
      printf("oom ram+swap exhausted\n");
      return 0;
    }

    memset((char *)stolen_pa, 5, PGSIZE);
    return stolen_pa;
  }
  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

void *evict_page()
{
  acquire(&frame_lock);

  int best_victim_index = -1;
  int worst_priority = -1;
  int found = 0;
  int stop_index = -1; // Keep track of where we broke the loop

  for (int pass = 0; pass < 2; pass++)
  {
    for (int step = 0; step < MAX_NFRAME; step++)
    {
      int i = (clock_hand + step) % MAX_NFRAME;

      if (frameTable[i].in_use == 1)
      {
        struct proc *p = frameTable[i].proc;
        uint64 va = frameTable[i].va;
        pte_t *pte = walk(p->pagetable, va, 0);

        if (pte != 0 && (*pte & PTE_V))
        {
          if (*pte & PTE_A)
          {
            // Case 1: We hit a 1, but haven't found a victim yet.
            if (!found)
            {
              *pte &= ~PTE_A;
              sfence_vma_addr(va); // Flush local TLB
            }
            // Case 2: We hit a 1, AND we already have a victim!
            else
            {
              stop_index = i; // Save where the clock hand should start next time
              break;
            }
          }
          else
          {
            // Case 3: We found our very first 0
            if (!found)
            {
              best_victim_index = i;
              worst_priority = p->queue_level;
              found = 1;
            }
            // Case 4: We found another 0, check if priority is worse (higher queue level)
            else
            {
              if (p->queue_level > worst_priority)
              {
                best_victim_index = i;
                worst_priority = p->queue_level;
              }
            }
          }
        }
      }
    }
    if (best_victim_index != -1)
    {
      break;
    }
  }

  if (best_victim_index == -1)
  {
    panic("evict_page: 100% memory deadlock, no victim found");
  }

  if (stop_index != -1)
  {
    clock_hand = stop_index; // Start at the '1' we stopped at
  }
  else
  {
    clock_hand = (best_victim_index + 1) % MAX_NFRAME;
  }

  void *victim_pa = frameTable[best_victim_index].pa;
  struct proc *victim_p = frameTable[best_victim_index].proc;
  uint64 victim_va = frameTable[best_victim_index].va;

  // printf("evict pid=%d q=%d va=%ld pa=%p\n", victim_p->pid, victim_p->queue_level, victim_va, victim_pa);

  if (swap_out(victim_va, victim_p->pagetable, victim_pa) == -1)
  {
    release(&frame_lock);
    return 0; // Return 0 to signal out-of-memory
  }

  // printf("swapped out pid=%d va=%ld to swap\n", victim_p->pid, victim_va);

  acquire(&victim_p->lock);
  victim_p->pages_swapped_out++;
  victim_p->pages_evicted++;
  victim_p->resident_pages--;
  release(&victim_p->lock);

  memset(victim_pa, 0, PGSIZE);
  //manually clear the frame table to entry to avoid race condition
  frameTable[best_victim_index].in_use = 0;
  frameTable[best_victim_index].proc = 0;
  frameTable[best_victim_index].va = 0;
  frameTable[best_victim_index].pa = 0;
  release(&frame_lock);

  return victim_pa;
}