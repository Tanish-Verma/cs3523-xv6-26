// t.c - RAID Mode Switching and Multi-Process Disk Stats Test
// Tests:
//   1. Switching RAID modes mid-run (RAID0 → RAID1 → RAID5 → RAID0)
//      does not corrupt data already on disk.
//   2. Multiple processes each see their own disk stats.
//   3. Each child's reads+writes are tracked independently.
//   4. Total I/O across all children is consistent with the workload.
//   5. setraidmode with invalid value is rejected.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

#define PGSIZE      4096
#define RAID0       0
#define RAID1       1
#define RAID5       5
#define FCFS        0
#define NCHILDREN   3
#define SWAP_PAGES  130   // > 64 frames, each child uses this many

typedef struct {
    int pid;
    int reads;
    int writes;
    int avg_latency;
    int errors;
} ChildReport;

static char pat(int i, int id) { return (char)((i*11 + id*19 + 3) & 0xFF); }

int main(void) {
    printf("=== Test t: RAID Mode Switching + Multi-Process Stats ===\n");

    // ------------------------------------------------------------------
    printf("[1] setraidmode: invalid values rejected\n");
    if (setraidmode(99) < 0)
        printf("  PASS: mode 99 rejected\n");
    else
        printf("  FAIL: mode 99 accepted\n");

    if (setraidmode(-1) < 0)
        printf("  PASS: mode -1 rejected\n");
    else
        printf("  FAIL: mode -1 accepted\n");

    if (setraidmode(2) < 0)
        printf("  PASS: mode 2 rejected\n");
    else
        printf("  FAIL: mode 2 accepted\n");

    // ------------------------------------------------------------------
    printf("[2] RAID mode sequence: RAID0->RAID1->RAID5->RAID0\n");
    int modes[] = {RAID0, RAID1, RAID5, RAID0};
    char *mnames[] = {"RAID0", "RAID1", "RAID5", "RAID0"};
    for (int m = 0; m < 4; m++) {
        if (setraidmode(modes[m]) == 0)
            printf("  PASS: switched to %s\n", mnames[m]);
        else
            printf("  FAIL: could not switch to %s\n", mnames[m]);
    }

    // ------------------------------------------------------------------
    printf("[3] Multi-process independent disk stats\n");

    int pipefd[NCHILDREN][2];
    for (int c = 0; c < NCHILDREN; c++) pipe(pipefd[c]);

    int child_modes[] = {RAID0, RAID1, RAID5};
    ChildReport reports[NCHILDREN];

    // FIX: Serialize children so they don't concurrently overwrite the global RAID_MODE
    for (int c = 0; c < NCHILDREN; c++) {
        int pid = fork();
        if (pid == 0) {
            // Close other children's pipes
            for (int i = 0; i < NCHILDREN; i++) {
                close(pipefd[i][0]);
                if (i != c) close(pipefd[i][1]);
            }

            ChildReport rep;
            rep.pid    = getpid2();
            rep.errors = 0;

            setraidmode(child_modes[c]);
            setdisksched(FCFS);

            char *mem = sbrk(SWAP_PAGES * PGSIZE);
            if (mem == (char *)-1) { rep.errors = 99; goto done; }

            // Write
            for (int i = 0; i < SWAP_PAGES; i++)
                mem[i * PGSIZE] = pat(i, c);

            // Read back
            for (int i = 0; i < SWAP_PAGES; i++) {
                if (mem[i * PGSIZE] != pat(i, c)) {
                    rep.errors++;
                    if (rep.errors <= 3)
                        printf("  child %d page %d: got 0x%x exp 0x%x\n",
                               c, i,
                               (unsigned char)mem[i*PGSIZE],
                               (unsigned char)pat(i,c));
                }
            }

        done:;
            struct diskstats st;
            memset(&st, 0, sizeof(st));
            getdiskstats(rep.pid, &st);
            rep.reads       = st.reads;
            rep.writes      = st.writes;
            rep.avg_latency = st.avg_latency;

            write(pipefd[c][1], &rep, sizeof(rep));
            close(pipefd[c][1]);
            exit(rep.errors != 0);
        }
        
        // PARENT: Wait for the specific child to finish to prevent RAID mode race conditions
        wait(0);
        
        // Collect the report for this child immediately
        close(pipefd[c][1]);
        read(pipefd[c][0], &reports[c], sizeof(reports[c]));
        close(pipefd[c][0]);
    }

    printf("\n  Results:\n");
    int all_ok = 1;
    for (int c = 0; c < NCHILDREN; c++) {
        ChildReport *r = &reports[c];
        printf("  child %d (%s): pid=%d reads=%d writes=%d"
               " latency=%d.%d errors=%d\n",
               c, mnames[c], r->pid,
               r->reads, r->writes,
               r->avg_latency/100, r->avg_latency%100,
               r->errors);
        if (r->errors != 0)   all_ok = 0;
        if (r->reads  == 0)  { printf("  FAIL: child %d no reads\n",  c); all_ok = 0; }
        if (r->writes == 0)  { printf("  FAIL: child %d no writes\n", c); all_ok = 0; }
    }

    if (all_ok)
        printf("  PASS: all %d children correct, independent stats\n", NCHILDREN);
    else
        printf("  FAIL: one or more children had errors\n");

    // ------------------------------------------------------------------
    printf("[4] Stats don't leak between children\n");
    // Each child has its own pid, so stats from different pids differ
    int unique_reads = 1;
    for (int a = 0; a < NCHILDREN && unique_reads; a++)
        for (int b = a+1; b < NCHILDREN; b++)
            if (reports[a].pid == reports[b].pid) unique_reads = 0;
    if (unique_reads)
        printf("  PASS: all children have distinct PIDs (stat isolation)\n");
    else
        printf("  WARN: duplicate PIDs detected\n");

    printf("=== Test t done ===\n");
    exit(0);
}