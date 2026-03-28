// test_sched_aware.c
// Tests scheduler-aware eviction: lower-priority (CPU-bound) children should
// lose pages before higher-priority (interactive/high-queue) children.
//
// Strategy:
//   - Fork a LOW-priority child: runs a CPU spin loop first, dropping its
//     MLFQ level, then allocates a working set.
//   - Fork a HIGH-priority child: allocates its working set immediately
//     (stays in top MLFQ queue).
//   - Parent forces memory pressure by allocating its own large region.
//   - Compare pages_evicted between the two children via getvmstats.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE  4096
#define WORK_PAGES 40      // working set per child
#define SPIN_ITERS 5000000 // enough to burn through MLFQ quanta

// Shared memory between parent and children (via pipes)
// Each child writes its PID to the pipe when ready.

int
main(void)
{
    int pipe_lo[2], pipe_hi[2];
    pipe(pipe_lo);
    pipe(pipe_hi);

    printf("=== test_sched_aware: Scheduler-Aware Eviction Test ===\n");

    // --- Fork LOW-priority child ---
    int pid_lo = fork();
    if (pid_lo == 0) {
        close(pipe_lo[0]);
        close(pipe_hi[0]); close(pipe_hi[1]);

        // Burn CPU to drop MLFQ priority
        volatile int x = 0;
        for (int i = 0; i < SPIN_ITERS; i++) x++;

        // Allocate and touch working set
        char *mem = sbrk((uint64)WORK_PAGES * PAGE_SIZE);
        for (int i = 0; i < WORK_PAGES; i++)
            mem[i * PAGE_SIZE] = (char)i;

        // Signal parent we're ready
        int mypid = getpid();
        write(pipe_lo[1], &mypid, sizeof(mypid));
        close(pipe_lo[1]);

        // Keep working set alive while parent applies pressure
        pause(20);

        // Verify data integrity after potential evictions
        int errs = 0;
        for (int i = 0; i < WORK_PAGES; i++)
            if (mem[i * PAGE_SIZE] != (char)i) errs++;
        if (errs == 0)
            printf("[LO-child] PASS: data intact after pressure\n");
        else
            printf("[LO-child] WARN: %d pages corrupted\n", errs);

        exit(0);
    }

    // --- Fork HIGH-priority child ---
    int pid_hi = fork();
    if (pid_hi == 0) {
        close(pipe_hi[0]);
        close(pipe_lo[0]); close(pipe_lo[1]);

        // Immediately allocate working set (stays at top MLFQ queue)
        char *mem = sbrk((uint64)WORK_PAGES * PAGE_SIZE);
        for (int i = 0; i < WORK_PAGES; i++)
            mem[i * PAGE_SIZE] = (char)(i + 100);

        int mypid = getpid();
        write(pipe_hi[1], &mypid, sizeof(mypid));
        close(pipe_hi[1]);

        pause(20);

        int errs = 0;
        for (int i = 0; i < WORK_PAGES; i++)
            if (mem[i * PAGE_SIZE] != (char)(i + 100)) errs++;
        if (errs == 0)
            printf("[HI-child] PASS: data intact after pressure\n");
        else
            printf("[HI-child] WARN: %d pages corrupted\n", errs);

        exit(0);
    }

    // --- Parent: wait for both children to be ready ---
    close(pipe_lo[1]); close(pipe_hi[1]);

    int cpid_lo, cpid_hi;
    read(pipe_lo[0], &cpid_lo, sizeof(cpid_lo));
    read(pipe_hi[0], &cpid_hi, sizeof(cpid_hi));
    close(pipe_lo[0]); close(pipe_hi[0]);

    printf("LO-priority child PID: %d\n", cpid_lo);
    printf("HI-priority child PID: %d\n", cpid_hi);

    // Apply memory pressure: allocate a large region to push others out
    int pressure_pages = 80;
    char *pressure = sbrk((uint64)pressure_pages * PAGE_SIZE);
    for (int i = 0; i < pressure_pages; i++)
        pressure[i * PAGE_SIZE] = (char)i;

    pause(5); // let eviction settle

    // Read stats for both children
    struct vmstats st_lo, st_hi;
    if (getvmstats(cpid_lo, &st_lo) != 0) {
        printf("FAIL: getvmstats for lo-child %d failed\n", cpid_lo);
    } else {
        printf("\n[LO-priority child %d]\n", cpid_lo);
        printf("  pages_evicted   : %d\n", st_lo.pages_evicted);
        printf("  pages_swapped_out:%d\n", st_lo.pages_swapped_out);
        printf("  resident_pages  : %d\n", st_lo.resident_pages);
    }

    if (getvmstats(cpid_hi, &st_hi) != 0) {
        printf("FAIL: getvmstats for hi-child %d failed\n", cpid_hi);
    } else {
        printf("[HI-priority child %d]\n", cpid_hi);
        printf("  pages_evicted   : %d\n", st_hi.pages_evicted);
        printf("  pages_swapped_out:%d\n", st_hi.pages_swapped_out);
        printf("  resident_pages  : %d\n", st_hi.resident_pages);
    }

    if (st_lo.pages_evicted >= st_hi.pages_evicted)
        printf("\nPASS: LO-priority process lost >= pages than HI-priority\n");
    else
        printf("\nFAIL: HI-priority process lost more pages than LO — check scheduler-aware eviction\n");

    // Reap children
    wait(0); wait(0);
    printf("=== test_sched_aware done ===\n");
    exit(0);
}
