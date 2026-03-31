// test_clock.c
// Tests Clock algorithm behavior indirectly:
// Pages accessed recently should survive longer than cold pages.
//
// Strategy:
//   1. Allocate a large region (hot + cold pages).
//   2. Keep touching hot pages in a loop (so their reference bits stay set).
//   3. Cold pages are never touched after initial fault.
//   4. Apply memory pressure.
//   5. Verify hot pages still hold correct data (they were protected by Clock).
//   6. Verify cold pages may have been evicted (we don't check their data).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE    4096
#define FRAME_LIMIT  32
#define HOT_PAGES    10         // kept active — should not be evicted
#define COLD_PAGES   15         // never touched again — likely evicted
#define PRESSURE_PAGES (FRAME_LIMIT * 2)

int
main(void)
{
    struct vmstats st;
    int pid = getpid();

    printf("=== test_clock: Clock Reference Bit Behavior ===\n");

    // Allocate hot + cold + pressure regions
    char *hot  = sbrk((uint64)HOT_PAGES * PAGE_SIZE);
    char *cold = sbrk((uint64)COLD_PAGES * PAGE_SIZE);

    if (hot == (char *)-1 || cold == (char *)-1) {
        printf("FAIL: sbrk\n");
        exit(1);
    }

    // Initial touch of both regions
    for (int i = 0; i < HOT_PAGES; i++)
        hot[i * PAGE_SIZE] = (char)(0xAA);

    for (int i = 0; i < COLD_PAGES; i++)
        cold[i * PAGE_SIZE] = (char)(0xBB);

    // Keep refreshing hot pages (simulate reference bit being set)
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < HOT_PAGES; i++)
            hot[i * PAGE_SIZE] = (char)(0xAA + round);
    }

    // Apply pressure to force evictions
    char *pressure = sbrk((uint64)PRESSURE_PAGES * PAGE_SIZE);
    if (pressure == (char *)-1) {
        printf("FAIL: sbrk for pressure\n");
        exit(1);
    }
    for (int i = 0; i < PRESSURE_PAGES; i++)
        pressure[i * PAGE_SIZE] = (char)i;

    // Check hot pages — these should NOT have been evicted by Clock
    printf("[Verifying HOT pages]\n");
    int hot_errs = 0;
    char expected_hot = (char)(0xAA + 4); // last value written
    for (int i = 0; i < HOT_PAGES; i++) {
        if (hot[i * PAGE_SIZE] != expected_hot) {
            printf("  FAIL: hot page %d corrupted (got 0x%x expected 0x%x)\n",
                   i,
                   (unsigned char)hot[i * PAGE_SIZE],
                   (unsigned char)expected_hot);
            hot_errs++;
        }
    }
    if (hot_errs == 0)
        printf("  PASS: all %d hot pages intact\n", HOT_PAGES);
    else
        printf("  FAIL: %d hot pages corrupted — Clock may not be protecting recently used pages\n",
               hot_errs);

    getvmstats(pid, &st);
    printf("\n--- VM Stats ---\n");
    printf("  page_faults      : %d\n", st.page_faults);
    printf("  pages_evicted    : %d\n", st.pages_evicted);
    printf("  pages_swapped_out: %d\n", st.pages_swapped_out);
    printf("  pages_swapped_in : %d\n", st.pages_swapped_in);
    printf("  resident_pages   : %d\n", st.resident_pages);

    if (st.pages_evicted > 0)
        printf("PASS: evictions occurred — Clock algorithm is running\n");
    else
        printf("WARN: no evictions — increase PRESSURE_PAGES or lower FRAME_LIMIT\n");

    printf("=== test_clock done ===\n");
    exit(0);
}
