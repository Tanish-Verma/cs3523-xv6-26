#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PGSIZE 4096

int main(void) {
  printf("\n--- Starting PA3 Virtual Memory & SC-MLFQ Test ---\n");

  int pid = fork();

  if (pid == 0) {
    // ==========================================
    // CHILD PROCESS: The "Low Priority Victim"
    // ==========================================
    int child_pid = getpid();
    printf("[Child %d] Allocating 2 MB of memory...\n", child_pid);
    
    // 1. Allocate a small chunk (500 pages = ~2 MB)
    char *child_mem = sbrk(500 * PGSIZE);
    for(int i = 0; i < 500; i++) {
      child_mem[i * PGSIZE] = 'C'; // Write data to force physical allocation
    }

    printf("[Child %d] Burning CPU to drop to the lowest MLFQ priority...\n", child_pid);
    // 2. Burn CPU ticks so the PA2 SC-MLFQ scheduler demotes this process
    for(volatile int i = 0; i < 50000000; i++) {
      // Busy waiting
    }

    printf("[Child %d] Sleeping. My pages are now vulnerable!\n", child_pid);
    // 3. Sleep to let the parent run and steal our memory
    pause(100); 

    // 4. Wake up and try to read our memory. 
    // This will cause Page Faults and force your OS to SWAP IN the data!
    printf("[Child %d] Woke up! Reading data to force SWAP-INS...\n", child_pid);
    int errors = 0;
    for(int i = 0; i < 500; i++) {
      if(child_mem[i * PGSIZE] != 'C') {
        errors++;
      }
    }
    
    if(errors == 0) {
      printf("[Child %d] SUCCESS! All 2 MB of swapped data survived.\n", child_pid);
    } else {
      printf("[Child %d] FAILURE! %d pages corrupted.\n", child_pid, errors);
    }
    
    exit(0);

  } else {
    // ==========================================
    // PARENT PROCESS: The "High Priority Hog"
    // ==========================================
    int parent_pid = getpid();
    
    // 1. Let the child run first so it can drop its priority
    pause(20); 

    printf("\n[Parent %d] Allocating 10 MB to exhaust the 8 MB Physical RAM...\n", parent_pid);
    
    // 2. Allocate 2500 pages (~10 MB). 
    // Since RAM is only 8 MB, this GUARANTEES we run out of memory.
    char *parent_mem = sbrk(2500 * PGSIZE);

    // 3. Write to all 10 MB. This fires up your Clock Eviction Engine!
    for(int i = 0; i < 2500; i++) {
      parent_mem[i * PGSIZE] = 'P';
    }

    printf("[Parent %d] Massive allocation complete. Checking final stats...\n", parent_pid);
    
    // 4. Fetch the stats to prove the kernel tracked the chaos
    struct vmstats st;
    getvmstats(parent_pid, &st);
    printf("\n--- Parent %d VM Stats ---\n", parent_pid);
    printf("Page Faults: %d\n", st.page_faults);
    printf("Pages Evicted: %d\n", st.pages_evicted);
    printf("Resident Pages: %d\n", st.resident_pages);
    printf("--------------------------\n\n");

    wait(0); // Wait for child to finish its swap-in checks
    printf("--- PA3 Test Complete ---\n");
    exit(0);
  }
}