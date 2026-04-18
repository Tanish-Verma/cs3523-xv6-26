// test_swap.c
// Stress-tests swap correctness by repeatedly cycling through a large array
// that far exceeds physical memory, checking data integrity at each pass.
//
// This verifies:
//   - Swap-out stores correct data
//   - Swap-in restores correct data
//   - pages_swapped_in / pages_swapped_out counters increment

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE   4096
#define FRAME_LIMIT 32           // match your kernel's MAX_FRAMES
#define NUM_PAGES   (FRAME_LIMIT * 4)
#define NUM_PASSES  3

// Fill pattern: encodes both page index and pass number
static inline char pattern(int page, int pass) {
    return (char)((page * 7 + pass * 13) & 0xFF);
}

int
main(void)
{
    struct vmstats st;
    int pid = getpid();

    printf("=== test_swap: Swap Correctness Stress Test ===\n");
    printf("PID: %d | %d pages | %d passes\n", pid, NUM_PAGES, NUM_PASSES);

    char *mem = sbrk((uint64)NUM_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) {
        printf("FAIL: sbrk\n");
        exit(1);
    }

    int total_errs = 0;

    for (int pass = 0; pass < NUM_PASSES; pass++) {
        printf("\n--- Pass %d: writing patterns ---\n", pass);

        // Write unique pattern per page
        for (int i = 0; i < NUM_PAGES; i++)
            mem[i * PAGE_SIZE] = pattern(i, pass);

        // Read back (may cause additional evictions + swap-ins)
        printf("--- Pass %d: reading back ---\n", pass);
        int errs = 0;
        for (int i = 0; i < NUM_PAGES; i++) {
            char expected = pattern(i, pass);
            char got = mem[i * PAGE_SIZE];
            if (got != expected) {
                printf("  ERR page %d: expected 0x%x got 0x%x\n",
                       i, (unsigned char)expected, (unsigned char)got);
                errs++;
                if (errs > 10) { printf("  (stopping early)\n"); break; }
            }
        }
        total_errs += errs;

        getvmstats(pid, &st);
        printf("  page_faults      : %d\n", st.page_faults);
        printf("  pages_evicted    : %d\n", st.pages_evicted);
        printf("  pages_swapped_out: %d\n", st.pages_swapped_out);
        printf("  pages_swapped_in : %d\n", st.pages_swapped_in);
        printf("  resident_pages   : %d\n", st.resident_pages);

        if (errs == 0)
            printf("PASS: pass %d data integrity OK\n", pass);
        else
            printf("FAIL: pass %d had %d data errors\n", pass, errs);
    }

    printf("\n=== Summary ===\n");
    if (total_errs == 0)
        printf("PASS: all passes completed with correct data\n");
    else
        printf("FAIL: total data errors across all passes: %d\n", total_errs);

    // Verify swap-in counter is non-zero (we definitely swapped)
    getvmstats(pid, &st);
    if (st.pages_swapped_in > 0)
        printf("PASS: pages_swapped_in = %d (swap-in works)\n", st.pages_swapped_in);
    else
        printf("FAIL: pages_swapped_in == 0 — swap-in not being tracked\n");

    if (st.pages_swapped_out > 0)
        printf("PASS: pages_swapped_out = %d (swap-out works)\n", st.pages_swapped_out);
    else
        printf("FAIL: pages_swapped_out == 0\n");

    printf("=== test_swap done ===\n");
    exit(0);
}
