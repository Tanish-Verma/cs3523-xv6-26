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
#include "swap.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

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
extern struct superblock sb;

void initswapspace()
{
  initlock(&swap_lock, "swap_table_lock");
  for (int i = 0; i < NSWAPFRAMES; i++)
  {
    swapTable[i].in_use = 0;
    swapTable[i].pagetable = 0;
    swapTable[i].va = 0;
    for (int j = 0; j < 4; j++)
    {
      swapTable[i].blocks[j] = -1;
    }
  }
}
void swap_in(uint64 va, pagetable_t pagetable, void *new_pa)
{
  int blocks[4] = {-1, -1, -1, -1};
  int found = 0;

  acquire(&swap_lock);
  for(int i = 0; i < NSWAPFRAMES; i++) {
    if(swapTable[i].in_use == 1 &&
       swapTable[i].pagetable == pagetable &&
       swapTable[i].va == va) {

      for(int j = 0; j < 4; j++) {
        blocks[j] = swapTable[i].blocks[j];
        swapTable[i].blocks[j] = 0;
      }
      swapTable[i].in_use = 0;
      swapTable[i].pagetable = 0;
      swapTable[i].va = 0;
      found = 1;
      break;
    }
  }
  release(&swap_lock);  // release BEFORE any disk I/O

  if(!found) {
    printf("swap_in failed: va=%ld\n", va);
    panic("swap_in: page not found");
  }

  for(int j = 0; j < 4; j++) {
    struct buf *b = bread(ROOTDEV, blocks[j]);
    memmove((char*)new_pa + j * BSIZE, b->data, BSIZE);
    brelse(b);
    sbfree(ROOTDEV, blocks[j]);
  }

  // Phase 3: update PTE
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("swap_in: pte missing");
  int flags = PTE_FLAGS(*pte);
  *pte = PA2PTE(new_pa) | (flags & ~PTE_S) | PTE_V;
}

int swap_out(uint64 va, pagetable_t pagetable, void *pa_to_evict)
{

  int blocks[4] = {-1, -1, -1, -1};
  for (int j = 0; j < 4; j++)
  {
    blocks[j] = sballoc(ROOTDEV);
    if (blocks[j] == -1)
      panic("swap_out: sballoc failed");
  }

  acquire(&swap_lock);
  int slot = -1;
  int found = 0;

  for (int i = 0; i < NSWAPFRAMES; i++)
  {

    if (swapTable[i].in_use == 0)
    {
      swapTable[i].in_use = 1;
      swapTable[i].pagetable = pagetable;
      swapTable[i].va = va;
      for (int j = 0; j < 4; j++)
      {
        swapTable[i].blocks[j] = blocks[j];
      }
      
      pte_t *pte = walk(pagetable, va, 0);
      if (pte == 0)
        panic("swap_out: pte missing");

      // Clear Valid bit, Set Swap bit. Leave all other permission flags alone
      *pte = (*pte & ~PTE_V) | PTE_S;
      found = 1;
      slot = i;
      break;
    }
  }
  release(&swap_lock);
  if (!found)
  {
    for(int j = 0; j < 4; j++){
      sbfree(ROOTDEV, blocks[j]);
    }
    // return -1 to indicate that the swap out failed due to no free swap slots
    //  The process will be killed by the caller when it sees the -1 return value
    return -1;
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      if (swapTable[slot].blocks[i] != -1)
      {
        struct buf *b = bread(ROOTDEV, swapTable[slot].blocks[i]);
        memmove(b->data, (char *)pa_to_evict + i * BSIZE, BSIZE);
        bwrite(b);
        brelse(b);
      }
      else
      {
        panic("swap_out: invalid swap block");
      }
    }
  }

  return 0;
}

void swap_free(uint64 va, pagetable_t pagetable)
{
  acquire(&swap_lock);
  int found = 0;
  int blocks[4] = {-1, -1, -1, -1};
  for (int i = 0; i < NSWAPFRAMES; i++)
  {
    // Find the exact swap slot for this pagetable and virtual address
    if (swapTable[i].in_use == 1 && swapTable[i].pagetable == pagetable && swapTable[i].va == va)
    {

      // Clear the metadata
      swapTable[i].in_use = 0;
      swapTable[i].pagetable = 0;
      swapTable[i].va = 0;
      for (int j = 0; j < 4; j++)
      {
        blocks[j] = swapTable[i].blocks[j];
        swapTable[i].blocks[j] = -1;
      }
      found = 1;
      break;
    }
  }
  release(&swap_lock);

  if(found) {                      // ← only free if slot was found
    for(int i = 0; i < 4; i++) {
      if(blocks[i] != -1)          // ← extra safety check
        sbfree(ROOTDEV, blocks[i]);
    }
  }
   else {
    printf("swap_free: no swap slot found for va=%ld\n", va);
  }
  
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
  int stop_index = -1;

  for(int pass = 0; pass < 2; pass++) {
    for(int step = 0; step < MAX_NFRAME; step++) {
      int i = (clock_hand + step) % MAX_NFRAME;

      if(frameTable[i].in_use == 1) {
        struct proc *p = frameTable[i].proc;
        uint64 va = frameTable[i].va;
        pte_t *pte = walk(p->pagetable, va, 0);

        if(pte != 0 && (*pte & PTE_V)) {
          if(*pte & PTE_A) {
            if(!found) {
              *pte &= ~PTE_A;
              sfence_vma_addr(va);
            } else {
              stop_index = i;
              break;
            }
          } else {
            if(!found) {
              best_victim_index = i;
              worst_priority = p->queue_level;
              found = 1;
            } else {
              if(p->queue_level > worst_priority) {
                best_victim_index = i;
                worst_priority = p->queue_level;
              }
            }
          }
        }
      }
    }
    if(best_victim_index != -1)
      break;
  }

  if(best_victim_index == -1){

    panic("evict_page: no victim found");
  }

  // update clock hand
  if(stop_index != -1){
    clock_hand = stop_index;
  }
  else{
    clock_hand = (best_victim_index + 1) % MAX_NFRAME;
  }

  // snapshot everything we need from the frame table
  void *victim_pa    = frameTable[best_victim_index].pa;
  struct proc *victim_p = frameTable[best_victim_index].proc;
  uint64 victim_va   = frameTable[best_victim_index].va;

  // clear frame table entry while still holding lock
  // this prevents any other CPU from picking the same victim
  frameTable[best_victim_index].in_use = 0;
  frameTable[best_victim_index].proc   = 0;
  frameTable[best_victim_index].va     = 0;
  frameTable[best_victim_index].pa     = 0;

  // update stats while lock held
  victim_p->pages_evicted++;
  victim_p->resident_pages--;

  // release BEFORE swap_out — swap_out does disk I/O which sleeps
  release(&frame_lock);


  if(swap_out(victim_va, victim_p->pagetable, victim_pa) == -1) {
    // swap failed — frame entry already cleared, page is lost
    // caller will see 0 and signal OOM
    return 0;
  }

  if(victim_p == myproc()){
    sfence_vma_addr(victim_va);
  }

  victim_p->pages_swapped_out++;

  memset(victim_pa, 0, PGSIZE);

  return victim_pa;
}