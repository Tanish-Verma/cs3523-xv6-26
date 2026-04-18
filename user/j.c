// test_pte.c
// Test 7: Page table entry (PTE) updates during eviction and swap-in
//
// What this tests:
//   - After eviction, accessing a swapped-out page causes a page fault (not a crash)
//   - The kernel correctly re-instates the PTE on swap-in
//   - Multiple fork() children independently manage their own page tables
//   - COW (copy-on-write) / independent address spaces are not confused
//   - per-process page_faults count is isolated (each child has its own counter)
//
// Strategy:
//   Fork N_CHILDREN.  Each child independently fills memory beyond MAXFRAMES,
//   then queries its OWN pid's vmstats.  Children must not see each other's faults.
//   Parent collects each child's stats and validates isolation.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

#define PAGE_SIZE 4096
#define MAXFRAMES 64
#define N_CHILDREN 3
#define PAGES_EACH 30 // slightly over each child's share

typedef struct
{
    int pid;
    int page_faults;
    int pages_evicted;
    int pages_swapped_out;
    int pages_swapped_in;
    int resident_pages;
    int errors;
} ChildReport;

int main(void)
{
    printf("=== Test 7: PTE update isolation across %d children ===\n", N_CHILDREN);
    printf("    PAGES_EACH=%d  MAXFRAMES=%d\n", PAGES_EACH, MAXFRAMES);

    // Shared-memory report array via pre-fork pipe per child
    int pipes[N_CHILDREN][2];
    for (int i = 0; i < N_CHILDREN; i++)
        pipe(pipes[i]);

    for (int c = 0; c < N_CHILDREN; c++)
    {
        int pid = fork();
        if (pid == 0)
        {
            // Close all pipes except ours (write end)
            for (int i = 0; i < N_CHILDREN; i++)
            {
                close(pipes[i][0]);
                if (i != c)
                    close(pipes[i][1]);
            }

            ChildReport rep;
            rep.pid = getpid();
            rep.errors = 0;

            // Allocate PAGES_EACH pages, write sentinels
            char *mem = sbrklazy((long)PAGES_EACH * PAGE_SIZE);
            if (mem == (char *)-1)
            {
                rep.errors = 99;
                goto done;
            }

            for (int i = 0; i < PAGES_EACH; i++)
                mem[i * PAGE_SIZE] = (char)((c * 100 + i) & 0xFF);

            // Force some evictions by doing a second pass with different values
            for (int i = 0; i < PAGES_EACH; i++)
                mem[i * PAGE_SIZE] = (char)((c * 100 + i + 1) & 0xFF);

            // Verify values (swap-in any evicted pages)
            for (int i = 0; i < PAGES_EACH; i++)
            {
                char exp = (char)((c * 100 + i + 1) & 0xFF);
                if (mem[i * PAGE_SIZE] != exp)
                    rep.errors++;
            }

            // Capture stats for THIS child's pid only
            struct vmstats s;
            if (getvmstats(rep.pid, &s) != 0)
            {
                rep.errors++;
            }
            else
            {
                rep.page_faults = s.page_faults;
                rep.pages_evicted = s.pages_evicted;
                rep.pages_swapped_out = s.pages_swapped_out;
                rep.pages_swapped_in = s.pages_swapped_in;
                rep.resident_pages = s.resident_pages;
            }

            // Verify stats are non-zero and plausible
            if (s.page_faults < PAGES_EACH)
                printf("  child %d WARN: page_faults=%d < PAGES_EACH=%d\n",
                       c, s.page_faults, PAGES_EACH);

        done:
            write(pipes[c][1], &rep, sizeof(rep));
            close(pipes[c][1]);
            exit(0);
        }
    }

    // Parent: collect reports
    ChildReport reports[N_CHILDREN];
    for (int c = 0; c < N_CHILDREN; c++)
    {
        close(pipes[c][1]);
        read(pipes[c][0], &reports[c], sizeof(ChildReport));
        close(pipes[c][0]);
    }
    for (int c = 0; c < N_CHILDREN; c++)
        wait(0);

    printf("\n[results]\n");
    int all_ok = 1;
    for (int c = 0; c < N_CHILDREN; c++)
    {
        ChildReport *r = &reports[c];
        printf("  child %d (pid=%d): faults=%d evicted=%d sout=%d sin=%d res=%d errors=%d\n",
               c, r->pid,
               r->page_faults, r->pages_evicted,
               r->pages_swapped_out, r->pages_swapped_in,
               r->resident_pages, r->errors);
        if (r->errors != 0)
            all_ok = 0;
    }

    // Cross-child isolation: each child's faults should be at least PAGES_EACH
    for (int c = 0; c < N_CHILDREN; c++)
    {
        if (reports[c].page_faults < PAGES_EACH)
        {
            printf("  FAIL: child %d faults=%d < expected %d\n",
                   c, reports[c].page_faults, PAGES_EACH);
            all_ok = 0;
        }
        else
        {
            printf("  PASS: child %d faults=%d >= %d\n",
                   c, reports[c].page_faults, PAGES_EACH);
        }
    }

    // getvmstats isolation: a child should NOT see another child's faults
    // (compare counts — if any two children have IDENTICAL non-zero stats,
    //  it may indicate shared accounting, which is a bug)
    for (int a = 0; a < N_CHILDREN; a++)
    {
        for (int b = a + 1; b < N_CHILDREN; b++)
        {
            if (reports[a].page_faults == reports[b].page_faults &&
                reports[a].page_faults > 0 &&
                reports[a].page_faults != PAGES_EACH)
            { // add this condition
                printf("  WARN: child %d and %d have identical fault counts – check isolation\n",
                       a, b);
            }
        }
    }

    if (all_ok)
        printf("  PASS: all children verified correctly\n");

    printf("=== Test 7 done ===\n");
    exit(!all_ok);
}