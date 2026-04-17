#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

#define PGSIZE 4096
#define NUM_PAGES 80 // 80 pages > 64 MAX_NFRAME, guaranteed to trigger swap
#define FCFS 0
#define SSTF 1



void trigger_swap_workload() {
    char *pages[NUM_PAGES];

    printf("  [Test] Allocating %d pages to force swap-out...\n", NUM_PAGES);
    for(int i = 0; i < NUM_PAGES; i++) {
        pages[i] = malloc(PGSIZE);
        if(pages[i] == 0) {
            printf("  [Error] malloc failed at page %d\n", i);
            exit(1);
        }
        // Write to the first byte of the page. 
        // This forces the kernel to actually allocate a physical frame 
        // and triggers the page eviction logic when RAM runs out.
        pages[i][0] = 'A' + (i % 26);
    }

    printf("  [Test] Reading back memory to force swap-in...\n");
    for(int i = 0; i < NUM_PAGES; i++) {
        // Accessing the evicted pages will trigger page faults,
        // forcing the kernel to read them back from the simulated RAID disk.
        char val = pages[i][0];
        if (val != 'A' + (i % 26)) {
            printf("  [Error] Data corruption on page %d!\n", i);
        }
    }

    printf("  [Test] Freeing memory...\n");
    for(int i = 0; i < NUM_PAGES; i++) {
        free(pages[i]);
    }

}

int main(int argc, char *argv[]) {
    printf("=== Starting PA4 Disk & Swap Test ===\n");
    struct diskstats diskstat = {0, 0, 0};
    // Test 1: FCFS Scheduling
    printf("\n--- Setting Policy: FCFS ---\n");
    if(setdisksched(FCFS) < 0) {
        printf("Error: sys_setdisksched failed\n");
    } else {
        trigger_swap_workload();
    }
    getdiskstats(getpid2(),&diskstat);
    printf("\nDisk Stats (FCFS): Reads=%d, Writes=%d, Avg Latency=%d.%d ticks\n",
           diskstat.reads, diskstat.writes, diskstat.avg_latency / 100, diskstat.avg_latency % 100);

    diskstat.reads = diskstat.writes = diskstat.avg_latency = 0; // reset stats
    // Test 2: SSTF Scheduling
    printf("\n--- Setting Policy: SSTF ---\n");
    if(setdisksched(SSTF) < 0) {
        printf("Error: sys_setdisksched failed\n");
    } else {
        trigger_swap_workload();
    }
    getdiskstats(getpid2(),&diskstat);
    printf("\nDisk Stats (SSTF): Reads=%d, Writes=%d, Avg Latency=%d.%d ticks\n",
           diskstat.reads, diskstat.writes, diskstat.avg_latency / 100, diskstat.avg_latency % 100);

    printf("\n=== Tests Completed ===\n");
    exit(0);
}