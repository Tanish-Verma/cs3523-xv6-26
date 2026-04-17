
#include "kernel/types.h"
#include "kernel/vmstats.h"
#include "user/user.h"

#define PGSIZE     4096
#define NPAGES     64    // per child; 3 children = 105 pages >> NFRAME=64
#define NCHILDREN  3

// Each child uses a different pattern so cross-contamination is visible
#define PATTERN(child, page) ((char)((child) * 37 + (page) * 11 + 1))

int
child_work(int id)
{
    char *mem = sbrklazy(NPAGES * PGSIZE);
    if (mem == SBRK_ERROR) {
        printf("FAIL: child %d sbrklazy failed\n", id);
        return 1;
    }

    // Write
    for (int i = 0; i < NPAGES; i++) {
        mem[i * PGSIZE] = PATTERN(id, i);
    }

    // Read back -- may trigger swap-in if frames were stolen
    int ok = 1;
    for (int i = 0; i < NPAGES; i++) {
        char got = mem[i * PGSIZE];
        if (got != PATTERN(id, i)) {
            printf("FAIL: child %d page %d: expected 0x%x got 0x%x\n",
                   id, i,
                   (unsigned char)PATTERN(id, i),
                   (unsigned char)got);
            ok = 0;
        }
    }

    if (ok)
        printf("PASS: child %d all %d pages correct under pressure\n",
               id, NPAGES);
    else
        printf("FAIL: child %d data corrupted under pressure\n", id);

    // Print this child's own stats
    struct vmstats s;
    if (getvmstats(getpid(), &s) == 0) {
        printf("INFO: child %d -- faults=%d evicted=%d swapped_out=%d swapped_in=%d resident=%d\n",
               id, s.page_faults, s.pages_evicted,
               s.pages_swapped_out, s.pages_swapped_in, s.resident_pages);
    }

    return ok ? 0 : 1;
}

int
main(void)
{
    printf("=== vmswap5: Multi-Process Memory Pressure ===\n");

    int pids[NCHILDREN];

    for (int i = 0; i < NCHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            printf("FAIL: fork %d failed\n", i);
            exit(1);
        }
        if (pids[i] == 0) {
            exit(child_work(i));
        }
    }

    int all_ok = 1;
    for (int i = 0; i < NCHILDREN; i++) {
        int status = -1;
        wait(&status);
        if (status != 0) {
            printf("FAIL: child %d exited with status %d\n", i, status);
            all_ok = 0;
        }
    }

    if (all_ok)
        printf("PASS: all %d children survived memory pressure with correct data\n",
               NCHILDREN);
    else
        printf("FAIL: one or more children reported corruption under pressure\n");

    // Parent's own stats (may show eviction if it also touched memory)
    struct vmstats ps;
    if (getvmstats(getpid(), &ps) == 0) {
        printf("INFO: parent -- faults=%d evicted=%d swapped_out=%d swapped_in=%d resident=%d\n",
               ps.page_faults, ps.pages_evicted,
               ps.pages_swapped_out, ps.pages_swapped_in, ps.resident_pages);
    }

    exit(0);
}
