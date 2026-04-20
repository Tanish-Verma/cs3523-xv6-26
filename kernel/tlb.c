#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tlb_shootdown_lock;
uint64 global_tlb_seq = 0; // Sequence version
uint64 global_tlb_va = 0;
volatile int shootdown_acks[NCPU]; 
extern int active_harts[]; // Import from main.c

void tlb_shootdown_init() {
  initlock(&tlb_shootdown_lock, "tlb_shootdown");
  for(int i = 0; i < NCPU; i++) shootdown_acks[i] = 1; // Default to safe
}

void global_tlb_flush(uint64 va) {
  acquire(&tlb_shootdown_lock);
  global_tlb_seq++; // Increment sequence for new shootdown
  global_tlb_va = va; // Set the address to flush
  release(&tlb_shootdown_lock);
  
//   for(int i = 0; i < NCPU; i++) shootdown_acks[i] = 0; // Reset acks
//   // Wait for all active harts to acknowledge
//   for(int i = 0; i < NCPU; i++) {
//     if(active_harts[i]) { // Only wait for active harts
//       while(shootdown_acks[i] == 0) {
//         // Optionally, add a timeout or yield here to avoid deadlock
//       }
//     }
//   }
}