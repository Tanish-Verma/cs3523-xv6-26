// test_replacement.c
// Forces page replacement by allocating more pages than the physical frame limit.
// Verifies Clock eviction occurs and swapped pages can be read back.
//
// Assumes PHYS_FRAMES (the frame table size) is configured to something small,
// e.g. 32 frames = 128 KB. Adjust FRAME_LIMIT below to match your kernel config.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE   4096

// Set this to the number of physical frames your kernel allows for user pages.
// E.g. if you set MAX_FRAMES 32 in kalloc.c, set FRAME_LIMIT 32 here.
#define FRAME_LIMIT 64

// Allocate well beyond the frame limit to force eviction.
#define NUM_PAGES   (FRAME_LIMIT * 3)

int
main(void)
{
    struct vmstats before, after;
    int pid = getpid();

    printf("=== test_replacement: Clock Page Replacement Test ===\n");
    printf("PID: %d | Frame limit: %d | Allocating: %d pages\n",
           pid, FRAME_LIMIT, NUM_PAGES);

    char *mem = sbrk((uint64)NUM_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) {
        printf("FAIL: sbrk failed\n");
        exit(1);
    }

    // Snapshot stats before touching pages
    getvmstats(pid, &before);

    // --- Phase 1: Sequential write (fills frames, forces evictions) ---
    printf("[Phase 1] Writing %d pages sequentially...\n", NUM_PAGES);
    for (int i = 0; i < NUM_PAGES; i++) {
        // Write a recognizable pattern
        mem[i * PAGE_SIZE] = (char)(i & 0xFF);
    }

    getvmstats(pid, &after);
    printf("  page_faults      : %d\n", after.page_faults - before.page_faults);
    printf("  pages_evicted    : %d\n", after.pages_evicted - before.pages_evicted);
    printf("  pages_swapped_out: %d\n", after.pages_swapped_out - before.pages_swapped_out);
    printf("  resident_pages   : %d\n", after.resident_pages);

    if (after.pages_evicted > 0)
        printf("PASS: evictions occurred (%d)\n", after.pages_evicted);
    else
        printf("WARN: no evictions yet — increase NUM_PAGES or lower FRAME_LIMIT\n");

    // --- Phase 2: Read back ALL pages (exercises swap-in) ---
    printf("[Phase 2] Reading back all %d pages...\n", NUM_PAGES);
    int errs = 0;
    for (int i = 0; i < NUM_PAGES; i++) {
        char expected = (char)(i & 0xFF);
        if (mem[i * PAGE_SIZE] != expected) {
            printf("FAIL: page %d: expected %d got %d\n",
                   i, (int)(unsigned char)expected,
                   (int)(unsigned char)mem[i * PAGE_SIZE]);
            errs++;
            if (errs > 5) { printf("  (too many errors, stopping)\n"); break; }
        }
    }
    if (errs == 0)
        printf("PASS: all %d pages read back correctly after eviction/swap-in\n",
               NUM_PAGES);

    getvmstats(pid, &after);
    printf("[Phase 2 stats]\n");
    printf("  pages_swapped_in : %d\n", after.pages_swapped_in);
    printf("  resident_pages   : %d\n", after.resident_pages);

    if (after.pages_swapped_in > 0)
        printf("PASS: swap-ins occurred (%d)\n", after.pages_swapped_in);
    else
        printf("WARN: no swap-ins recorded\n");

    printf("=== test_replacement done ===\n");
    exit(0);
}
