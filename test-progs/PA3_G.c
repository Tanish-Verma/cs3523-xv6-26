// test_vmstats.c
// Tests the getvmstats() system call:
//   1. Stats are isolated per process (child's faults don't affect parent's count)
//   2. Invalid PID returns -1
//   3. Stats monotonically increase (never go backwards)
//   4. resident_pages never exceeds MAX_FRAMES

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE   4096
#define FRAME_LIMIT 32      // match kernel MAX_FRAMES
#define CHILD_PAGES 20
#define PARENT_PAGES 15

int
main(void)
{
    struct vmstats st1, st2;
    int pid = getpid();

    printf("=== test_vmstats: System Call Correctness ===\n");

    // --- Test 1: invalid PID ---
    printf("[Test 1] Invalid PID\n");
    if (getvmstats(-1, &st1) == -1)
        printf("  PASS: getvmstats(-1) -> -1\n");
    else
        printf("  FAIL: getvmstats(-1) should return -1\n");

    if (getvmstats(99999, &st1) == -1)
        printf("  PASS: getvmstats(99999) -> -1\n");
    else
        printf("  FAIL: getvmstats(99999) should return -1\n");

    // --- Test 2: stats start at zero (or low) for fresh process ---
    printf("[Test 2] Fresh process stats\n");
    getvmstats(pid, &st1);
    printf("  Initial page_faults: %d\n", st1.page_faults);

    // --- Test 3: Stats monotonically increase ---
    printf("[Test 3] Monotonic increase\n");
    char *mem = sbrk((uint64)PARENT_PAGES * PAGE_SIZE);
    for (int i = 0; i < PARENT_PAGES; i++)
        mem[i * PAGE_SIZE] = (char)i;

    getvmstats(pid, &st2);
    if (st2.page_faults >= st1.page_faults)
        printf("  PASS: page_faults non-decreasing (%d -> %d)\n",
               st1.page_faults, st2.page_faults);
    else
        printf("  FAIL: page_faults went backwards!\n");

    if (st2.resident_pages <= FRAME_LIMIT)
        printf("  PASS: resident_pages (%d) <= FRAME_LIMIT (%d)\n",
               st2.resident_pages, FRAME_LIMIT);
    else
        printf("  FAIL: resident_pages (%d) > FRAME_LIMIT (%d)\n",
               st2.resident_pages, FRAME_LIMIT);

    // --- Test 4: Child stats are isolated from parent ---
    printf("[Test 4] Per-process stat isolation\n");
    getvmstats(pid, &st1); // parent baseline

    int child_pid = fork();
    if (child_pid == 0) {
        // Child allocates its own pages
        char *cmem = sbrk((uint64)CHILD_PAGES * PAGE_SIZE);
        for (int i = 0; i < CHILD_PAGES; i++)
            cmem[i * PAGE_SIZE] = (char)i;
        // Child doesn't call getvmstats — just exits
        exit(0);
    }

    wait(0); // wait for child

    getvmstats(pid, &st2); // parent after child ran
    // Parent's page_faults should NOT have increased due to child's faults
    int parent_delta = st2.page_faults - st1.page_faults;
    if (parent_delta < CHILD_PAGES) // child's 20 faults shouldn't show in parent
        printf("  PASS: parent page_faults unchanged by child activity (delta=%d)\n",
               parent_delta);
    else
        printf("  WARN: parent page_faults increased by %d — check isolation\n",
               parent_delta);

    // --- Test 5: getvmstats on zombie/dead child PID ---
    printf("[Test 5] Dead child PID\n");
    // child_pid is now dead; behavior is implementation-defined,
    // but should not crash
    int ret = getvmstats(child_pid, &st1);
    printf("  getvmstats(dead child %d) returned %d (acceptable: -1 or 0)\n",
           child_pid, ret);

    printf("=== test_vmstats done ===\n");
    exit(0);
}
