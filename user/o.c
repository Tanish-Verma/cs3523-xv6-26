// o.c - RAID 1 (Mirroring) Correctness Test
// Tests:
//   1. setraidmode(RAID1) is accepted
//   2. Data survives swap-out/in under RAID1 (both mirrors written)
//   3. Simulated disk failure: setfaileddisk(d) causes reads from mirror
//   4. Data still correct after one disk fails (mirror serves reads)
//   5. Writes still succeed with one disk failed
//   6. setfaileddisk(-1) or out-of-range is rejected
//
// RAID1 writes each block to 2 disks; reads served from either.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

#define PGSIZE      4096
#define RAID1       1
#define SSTF        1
#define NDISKS      4
// Must exceed 64 frames
#define SWAP_PAGES  82 

static char pat(int i) { return (char)((i * 17 + 5) & 0xFF); }

static int run_write_read(char *mem, int npages, int pass) {
    for (int i = 0; i < npages; i++)
        mem[i * PGSIZE] = (char)((pat(i) + pass) & 0xFF);

    int errs = 0;
    for (int i = 0; i < npages; i++) {
        char expected = (char)((pat(i) + pass) & 0xFF);
        if (mem[i * PGSIZE] != expected) {
            errs++;
            if (errs <= 3)
                printf("  page %d: got 0x%x exp 0x%x\n",
                       i, (unsigned char)mem[i*PGSIZE],
                       (unsigned char)expected);
        }
    }
    return errs;
}

int main(void) {
    printf("=== Test o: RAID 1 Mirroring Correctness ===\n");

    // ------------------------------------------------------------------
    printf("[1] setfaileddisk: invalid values rejected\n");
    if (setfaileddisk(-1) < 0)
        printf("  PASS: disk -1 rejected\n");
    else
        printf("  FAIL: disk -1 accepted\n");

    if (setfaileddisk(NDISKS) < 0)
        printf("  PASS: disk %d rejected (out of range)\n", NDISKS);
    else
        printf("  FAIL: disk %d accepted\n", NDISKS);

    // ------------------------------------------------------------------
    printf("[2] setraidmode(RAID1)\n");
    if (setraidmode(RAID1) == 0)
        printf("  PASS: RAID1 set\n");
    else {
        printf("  FAIL: RAID1 rejected\n");
        exit(1);
    }
    setdisksched(SSTF);

    char *mem = sbrk(SWAP_PAGES * PGSIZE);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); exit(1); }

    // ------------------------------------------------------------------
    printf("[3] Normal RAID1 operation (no failed disk)\n");
    int errs = run_write_read(mem, SWAP_PAGES, 0);
    if (errs == 0)
        printf("  PASS: all %d pages correct under RAID1\n", SWAP_PAGES);
    else
        printf("  FAIL: %d errors under normal RAID1\n", errs);

    // ------------------------------------------------------------------
    printf("[4] RAID1 with disk 0 failed\n");
    if (setfaileddisk(0) == 0)
        printf("  disk 0 marked failed\n");
    else {
        printf("  FAIL: setfaileddisk(0) rejected\n");
        exit(1);
    }

    errs = run_write_read(mem, SWAP_PAGES, 1);
    if (errs == 0)
        printf("  PASS: all pages correct with disk 0 failed (mirror read)\n");
    else
        printf("  FAIL: %d errors with disk 0 failed\n", errs);

    // ------------------------------------------------------------------
    printf("[5] RAID1 with disk 1 failed\n");
    if (setfaileddisk(1) == 0)
        printf("  disk 1 marked failed\n");
    else
        printf("  FAIL: setfaileddisk(1) rejected\n");

    errs = run_write_read(mem, SWAP_PAGES, 2);
    if (errs == 0)
        printf("  PASS: all pages correct with disk 1 failed\n");
    else
        printf("  FAIL: %d errors with disk 1 failed\n", errs);

    // ------------------------------------------------------------------
    printf("[6] Disk I/O stats plausible for RAID1\n");
    // RAID1 writes each block twice (primary + mirror)
    struct diskstats st;
    memset(&st, 0, sizeof(st));
    getdiskstats(getpid2(), &st);
    printf("  reads=%d writes=%d avg_latency=%d.%d\n",
           st.reads, st.writes, st.avg_latency/100, st.avg_latency%100);
    if (st.writes > 0 && st.reads > 0)
        printf("  PASS: I/O recorded\n");
    else
        printf("  FAIL: missing reads or writes\n");

    // Restore: no failed disk (use disk 3 as sentinel — will be reset
    // at next setraidmode call in later tests)
    printf("=== Test o done ===\n");
    exit(0);
}
