// u.c - Disk-Backed Swap End-to-End Stress Test
// Tests:
//   1. Swap-out writes correct data to disk (not memory).
//   2. Swap-in reads the exact bytes back after eviction.
//   3. Multiple rounds of eviction+restore do not accumulate errors.
//   4. Large allocation (3x frame limit) exercises the full swap path.
//   5. fork() + swap: child data is independent of parent.
//   6. Process exits cleanly with outstanding swapped pages
//      (kernel must free swap blocks on exit).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"
#include "kernel/vmstats.h"

#define PGSIZE      4096
#define FRAME_LIMIT 64
// 3x frame limit forces heavy eviction
#define NUM_PAGES   (FRAME_LIMIT * 3)
#define PASSES      4
#define RAID0       0
#define SSTF        1

static char pat(int page, int pass) {
    return (char)((page * 97 + pass * 53 + 7) & 0xFF);
}

int main(void) {
    printf("=== Test u: Disk-Backed Swap End-to-End Stress ===\n");

    setraidmode(RAID0);
    setdisksched(SSTF);

    int pid = getpid2();
    struct vmstats vm_before, vm_after;
    struct diskstats dk_before, dk_after;
    memset(&vm_before, 0, sizeof(vm_before));
    memset(&dk_before, 0, sizeof(dk_before));

    char *mem = sbrk(NUM_PAGES * PGSIZE);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); exit(1); }

    getvmstats(pid, &vm_before);
    getdiskstats(pid, &dk_before);

    // ------------------------------------------------------------------
    printf("[1] Multi-pass write+read over %d pages\n", NUM_PAGES);
    int total_errs = 0;
    for (int pass = 0; pass < PASSES; pass++) {
        // Write entire region
        for (int i = 0; i < NUM_PAGES; i++)
            mem[i * PGSIZE] = pat(i, pass);

        // Read back — forces swap-in for evicted pages
        int errs = 0;
        for (int i = 0; i < NUM_PAGES; i++) {
            char expected = pat(i, pass);
            char got = mem[i * PGSIZE];
            if (got != expected) {
                errs++;
                if (errs <= 5)
                    printf("  page %d pass %d: got 0x%x exp 0x%x\n",
                           i, pass, (unsigned char)got, (unsigned char)expected);
            }
        }
        total_errs += errs;
        if (errs == 0)
            printf("  pass %d PASS (%d pages OK)\n", pass, NUM_PAGES);
        else
            printf("  pass %d FAIL (%d errors)\n", pass, errs);
    }

    getvmstats(pid, &vm_after);
    getdiskstats(pid, &dk_after);

    printf("\n  VM stats: faults=%d evicted=%d sin=%d sout=%d res=%d\n",
           vm_after.page_faults, vm_after.pages_evicted,
           vm_after.pages_swapped_in, vm_after.pages_swapped_out,
           vm_after.resident_pages);
    printf("  Disk stats: reads=%d writes=%d latency=%d.%d\n",
           dk_after.reads, dk_after.writes,
           dk_after.avg_latency/100, dk_after.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[2] Evictions and swap I/O occurred\n");
    if (vm_after.pages_evicted > 0)
        printf("  PASS: %d evictions\n", vm_after.pages_evicted);
    else
        printf("  FAIL: no evictions — frame limit may not be enforced\n");

    if (vm_after.pages_swapped_in > 0)
        printf("  PASS: %d swap-ins\n", vm_after.pages_swapped_in);
    else
        printf("  FAIL: no swap-ins recorded\n");

    if (dk_after.writes > dk_before.writes)
        printf("  PASS: disk writes increased (%d)\n",
               dk_after.writes - dk_before.writes);
    else
        printf("  FAIL: no new disk writes\n");

    if (dk_after.reads > dk_before.reads)
        printf("  PASS: disk reads increased (%d)\n",
               dk_after.reads - dk_before.reads);
    else
        printf("  FAIL: no new disk reads\n");

    // ------------------------------------------------------------------
    printf("[3] Overall data integrity\n");
    if (total_errs == 0)
        printf("  PASS: all %d passes x %d pages correct\n", PASSES, NUM_PAGES);
    else
        printf("  FAIL: %d total data errors across %d passes\n",
               total_errs, PASSES);

    // ------------------------------------------------------------------
    printf("[4] fork() + swap: child data independent of parent\n");
    // Write one more pass in parent
    for (int i = 0; i < NUM_PAGES; i++)
        mem[i * PGSIZE] = pat(i, 99);

    int child = fork();
    if (child == 0) {
        // Child overwrites with different pattern
        for (int i = 0; i < NUM_PAGES; i++)
            mem[i * PGSIZE] = pat(i, 200);
        // Verify child pattern
        int errs = 0;
        for (int i = 0; i < NUM_PAGES; i++)
            if (mem[i * PGSIZE] != pat(i, 200)) errs++;
        if (errs == 0)
            printf("  child PASS: own pattern correct after fork\n");
        else
            printf("  child FAIL: %d errors\n", errs);
        exit(0);
    }
    wait(0);

    // Parent pattern must be undisturbed
    int errs = 0;
    for (int i = 0; i < NUM_PAGES; i++)
        if (mem[i * PGSIZE] != pat(i, 99)) errs++;
    if (errs == 0)
        printf("  parent PASS: parent pattern intact after child fork\n");
    else
        printf("  parent FAIL: %d parent pages corrupted by child\n", errs);

    // ------------------------------------------------------------------
    printf("[5] Process cleans up swap on exit (no panic)\n");
    // Allocate extra pages and exit without reading them back
    // — kernel must free their swap slots
    int cleanup_child = fork();
    if (cleanup_child == 0) {
        setraidmode(RAID0);
        setdisksched(SSTF);
        char *extra = sbrk(FRAME_LIMIT * 2 * PGSIZE);
        if (extra != (char *)-1) {
            for (int i = 0; i < FRAME_LIMIT * 2; i++)
                extra[i * PGSIZE] = (char)i;
        }
        // Exit without reading back — tests swap block cleanup on exit
        exit(0);
    }
    wait(0);
    printf("  PASS: child exited cleanly with outstanding swapped pages\n");

    printf("=== Test u done (total_errs=%d) ===\n", total_errs);
    exit(total_errs != 0);
}
