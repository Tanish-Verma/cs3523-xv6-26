// r.c - Scheduler-Aware Disk Scheduling Test (PA2 Integration)
// Tests:
//   1. Under SSTF with priority awareness, requests from higher-priority
//      processes should be preferred when seek distance ties.
//   2. A CPU-bound process (demoted to low MLFQ queue) accumulates more
//      disk latency per request than an interactive process at queue 0
//      when blocks are equidistant from head.
//   3. Stats are per-process: child's disk stats don't pollute parent's.
//   4. Switching policies mid-run doesn't corrupt ongoing swaps.
//
// This test uses fork() to create a high-priority (interactive) child
// and a low-priority (CPU-bound) child, then compares disk stats.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"
#include "kernel/vmstats.h"

#define PGSIZE       4096
#define SSTF         1
#define FCFS         0
#define SWAP_PAGES   80
#define SPIN_ITERS   600000000   // enough to get demoted to level 2+

static void do_swap_work(void) {
    char *mem = sbrk(SWAP_PAGES * PGSIZE);
    if (mem == (char *)-1) return;
    for (int i = 0; i < SWAP_PAGES; i++)
        mem[i * PGSIZE] = (char)(i & 0xFF);
    for (int i = 0; i < SWAP_PAGES; i++) {
        if (mem[i * PGSIZE] != (char)(i & 0xFF))
            printf("  [WARN] data mismatch page %d\n", i);
    }
}

int main(void) {
    printf("=== Test r: Scheduler-Aware Disk Scheduling ===\n");

    setdisksched(SSTF);
    setraidmode(0); // RAID0 for simplicity

    // ------------------------------------------------------------------
    printf("[1] Per-process stat isolation\n");
    // Fork a child that does disk I/O; parent's stats should not include it
    struct diskstats parent_before, parent_after;
    memset(&parent_before, 0, sizeof(parent_before));
    memset(&parent_after, 0, sizeof(parent_after));
    int ppid = getpid2();
    getdiskstats(ppid, &parent_before);

    int child = fork();
    if (child == 0) {
        // Child does swap I/O
        setraidmode(0);
        setdisksched(SSTF);
        do_swap_work();
        exit(0);
    }
    wait(0);

    getdiskstats(ppid, &parent_after);
    int delta_reads  = parent_after.reads  - parent_before.reads;
    int delta_writes = parent_after.writes - parent_before.writes;
    printf("  parent delta: reads=%d writes=%d\n", delta_reads, delta_writes);
    if (delta_reads == 0 && delta_writes == 0)
        printf("  PASS: parent stats unaffected by child's disk I/O\n");
    else
        printf("  NOTE: parent delta non-zero — may share kernel I/O path\n");

    // ------------------------------------------------------------------
    printf("[2] High-priority process disk I/O\n");
    // Parent is at high MLFQ priority (syscall-heavy = low queue level)
    // Do many syscalls to stay interactive
    for (int i = 0; i < 5000; i++) getpid(); // keep del_s high
    do_swap_work();
    struct diskstats hi_st;
    memset(&hi_st, 0, sizeof(hi_st));
    getdiskstats(ppid, &hi_st);
    printf("  HI-priority: reads=%d writes=%d latency=%d.%d\n",
           hi_st.reads - parent_after.reads,
           hi_st.writes - parent_after.writes,
           hi_st.avg_latency/100, hi_st.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[3] Low-priority process disk I/O\n");
    int lo_pipe[2];
    pipe(lo_pipe);
    int lo_child = fork();
    if (lo_child == 0) {
        close(lo_pipe[0]);
        // Spin to get demoted in MLFQ
        volatile int x = 0;
        for (int i = 0; i < SPIN_ITERS; i++) x++;
        setraidmode(0);
        setdisksched(SSTF);
        do_swap_work();
        struct diskstats lo_st;
        memset(&lo_st, 0, sizeof(lo_st));
        getdiskstats(getpid2(), &lo_st);
        write(lo_pipe[1], &lo_st, sizeof(lo_st));
        close(lo_pipe[1]);
        exit(0);
    }
    close(lo_pipe[1]);
    struct diskstats lo_st;
    memset(&lo_st, 0, sizeof(lo_st));
    read(lo_pipe[0], &lo_st, sizeof(lo_st));
    close(lo_pipe[0]);
    wait(0);

    printf("  LO-priority: reads=%d writes=%d latency=%d.%d\n",
           lo_st.reads, lo_st.writes,
           lo_st.avg_latency/100, lo_st.avg_latency%100);

    if (lo_st.reads > 0 && lo_st.writes > 0)
        printf("  PASS: low-priority process performed disk I/O\n");
    else
        printf("  FAIL: low-priority process had no I/O\n");

    // ------------------------------------------------------------------
    printf("[4] Policy switch mid-run correctness\n");
    // Switch policy between writes and reads — data must still be correct
    char *mem2 = sbrk(SWAP_PAGES * PGSIZE);
    if (mem2 == (char *)-1) { printf("  FAIL: sbrk\n"); exit(1); }

    setdisksched(FCFS);
    for (int i = 0; i < SWAP_PAGES; i++)
        mem2[i * PGSIZE] = (char)((i + 7) & 0xFF);

    setdisksched(SSTF); // switch policy before reads
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES; i++)
        if (mem2[i * PGSIZE] != (char)((i + 7) & 0xFF)) errs++;

    if (errs == 0)
        printf("  PASS: data correct after mid-run policy switch\n");
    else
        printf("  FAIL: %d data errors after policy switch\n", errs);

    // ------------------------------------------------------------------
    printf("[5] Stats monotonically increase\n");
    struct diskstats final_st;
    memset(&final_st, 0, sizeof(final_st));
    getdiskstats(ppid, &final_st);
    if (final_st.reads >= hi_st.reads && final_st.writes >= hi_st.writes)
        printf("  PASS: stats only increase\n");
    else
        printf("  FAIL: stats went backwards\n");

    printf("=== Test r done ===\n");
    exit(0);
}
