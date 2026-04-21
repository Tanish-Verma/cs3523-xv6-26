// l.c - Basic Disk Scheduling System Call Test
// Tests:
//   1. setdisksched() accepts FCFS (0) and SSTF (1)
//   2. setdisksched() rejects invalid policies
//   3. getdiskstats() accumulates reads/writes/latency after swap I/O
//   4. getdiskstats() rejects invalid PIDs
//
// Run this first — it is the minimal sanity check for PA4 system calls.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

#define PGSIZE     4096
#define FCFS       0
#define SSTF       1
// 80 pages > 64-frame limit, guaranteed to trigger swap I/O
#define SWAP_PAGES 80

static void force_swap_io(void) {
    char *mem[SWAP_PAGES];
    // Allocate and touch pages to force evictions
    for (int i = 0; i < SWAP_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        if (!mem[i]) { printf("  malloc failed at %d\n", i); return; }
        mem[i][0] = (char)(i & 0xFF);
        mem[i][PGSIZE - 1] = (char)((i + 1) & 0xFF);
    }
    // Read back all pages — triggers swap-in for evicted ones
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES; i++) {
        if (mem[i][0] != (char)(i & 0xFF)) errs++;
        if (mem[i][PGSIZE - 1] != (char)((i + 1) & 0xFF)) errs++;
    }
    if (errs == 0)
        printf("  data integrity OK (%d pages)\n", SWAP_PAGES);
    else
        printf("  WARN: %d data mismatches\n", errs);
    for (int i = 0; i < SWAP_PAGES; i++) free(mem[i]);
}

int main(void) {
    printf("=== Test l: Basic Disk Scheduling System Call Sanity ===\n");
    struct diskstats st;
    int pid = getpid2();

    // -----------------------------------------------------------------------
    printf("[1] setdisksched: invalid policies must be rejected\n");
    if (setdisksched(99) < 0)
        printf("  PASS: policy 99 rejected\n");
    else
        printf("  FAIL: policy 99 accepted\n");

    if (setdisksched(-1) < 0)
        printf("  PASS: policy -1 rejected\n");
    else
        printf("  FAIL: policy -1 accepted\n");

    if (setdisksched(2) < 0)
        printf("  PASS: policy 2 rejected\n");
    else
        printf("  FAIL: policy 2 accepted\n");

    // -----------------------------------------------------------------------
    printf("[2] setdisksched(FCFS) then trigger swap I/O\n");
    if (setdisksched(FCFS) == 0)
        printf("  PASS: FCFS accepted\n");
    else {
        printf("  FAIL: FCFS rejected — aborting\n");
        exit(1);
    }

    force_swap_io();

    memset(&st, 0, sizeof(st));
    if (getdiskstats(pid, &st) != 0) {
        printf("  FAIL: getdiskstats returned error\n");
        exit(1);
    }
    printf("  FCFS stats -> reads=%d writes=%d avg_latency=%d.%d ticks\n",
           st.reads, st.writes, st.avg_latency / 100, st.avg_latency % 100);

    if (st.writes > 0)
        printf("  PASS: disk_writes > 0 (swap-out happened)\n");
    else
        printf("  FAIL: disk_writes == 0\n");

    if (st.reads > 0)
        printf("  PASS: disk_reads > 0 (swap-in happened)\n");
    else
        printf("  FAIL: disk_reads == 0\n");

    if (st.avg_latency > 0)
        printf("  PASS: avg_latency > 0\n");
    else
        printf("  FAIL: avg_latency == 0 — latency model not running\n");

    // -----------------------------------------------------------------------
    printf("[3] setdisksched(SSTF)\n");
    if (setdisksched(SSTF) == 0)
        printf("  PASS: SSTF accepted\n");
    else
        printf("  FAIL: SSTF rejected\n");

    // -----------------------------------------------------------------------
    printf("[4] getdiskstats: invalid PID handling\n");
    struct diskstats bad;
    memset(&bad, 0, sizeof(bad));
    if (getdiskstats(-1, &bad) < 0)
        printf("  PASS: PID -1 rejected\n");
    else
        printf("  FAIL: PID -1 accepted\n");

    if (getdiskstats(99999, &bad) < 0)
        printf("  PASS: PID 99999 rejected\n");
    else
        printf("  FAIL: PID 99999 accepted\n");

    // -----------------------------------------------------------------------
    printf("[5] stats are non-negative\n");
    if (st.reads >= 0 && st.writes >= 0 && st.avg_latency >= 0)
        printf("  PASS: all counters non-negative\n");
    else
        printf("  FAIL: negative counter detected\n");

    printf("=== Test l done ===\n");
    exit(0);
}
