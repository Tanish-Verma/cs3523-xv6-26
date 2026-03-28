// test_basic_pf.c
// Tests basic page fault handling and getvmstats system call.
// Allocates memory, touches pages, then reads back vmstats.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"   // struct vmstats

#define PAGE_SIZE 4096
#define NUM_PAGES 10

int
main(void)
{
    struct vmstats st;
    int pid = getpid();

    printf("=== test_basic_pf: Basic Page Fault Test ===\n");
    printf("PID: %d\n", pid);

    // Allocate NUM_PAGES pages via sbrk
    char *mem = sbrk(NUM_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) {
        printf("FAIL: sbrk failed\n");
        exit(1);
    }

    // Touch each page to trigger a page fault per page
    for (int i = 0; i < NUM_PAGES; i++) {
        mem[i * PAGE_SIZE] = (char)(i + 1);
    }

    // Read back to verify correctness
    int ok = 1;
    for (int i = 0; i < NUM_PAGES; i++) {
        if (mem[i * PAGE_SIZE] != (char)(i + 1)) {
            printf("FAIL: data mismatch at page %d\n", i);
            ok = 0;
        }
    }
    if (ok)
        printf("PASS: all %d pages written and read back correctly\n", NUM_PAGES);

    // Query vmstats
    if (getvmstats(pid, &st) != 0) {
        printf("FAIL: getvmstats returned error\n");
        exit(1);
    }

    printf("\n--- VM Stats for PID %d ---\n", pid);
    printf("  page_faults     : %d\n", st.page_faults);
    printf("  pages_evicted   : %d\n", st.pages_evicted);
    printf("  pages_swapped_in: %d\n", st.pages_swapped_in);
    printf("  pages_swapped_out:%d\n", st.pages_swapped_out);
    printf("  resident_pages  : %d\n", st.resident_pages);

    if (st.page_faults >= NUM_PAGES)
        printf("PASS: page_faults (%d) >= %d\n", st.page_faults, NUM_PAGES);
    else
        printf("FAIL: expected at least %d page_faults, got %d\n",
               NUM_PAGES, st.page_faults);

    // Test invalid PID
    if (getvmstats(-1, &st) == -1)
        printf("PASS: getvmstats(-1) correctly returned -1\n");
    else
        printf("FAIL: getvmstats(-1) should return -1\n");

    printf("=== test_basic_pf done ===\n");
    exit(0);
}
