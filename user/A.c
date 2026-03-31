#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

static int
all_non_negative(const struct vmstats *s)
{
  return s->page_faults >= 0 &&
         s->pages_evicted >= 0 &&
         s->pages_swapped_in >= 0 &&
         s->pages_swapped_out >= 0 &&
         s->resident_pages >= 0;
}

int
main(void)
{
  struct vmstats before = {0};
  struct vmstats after = {0};
  int pid = getpid();
  int rc;

  rc = getvmstats(pid, &before);
  if (rc != 0)
  {
    printf("FAIL: getvmstats(self) returned %d\n", rc);
    exit(1);
  }

  if (!all_non_negative(&before))
  {
    printf("FAIL: negative counter in initial stats\n");
    exit(1);
  }

  // Touch two fresh pages to trigger lazy allocation/page-fault activity.
  char *base = sbrk(2 * 4096);
  if (base == (char *)-1)
  {
    printf("FAIL: sbrk failed\n");
    exit(1);
  }
  base[0] = 'x';
  base[4096] = 'y';

  rc = getvmstats(pid, &after);
  if (rc != 0)
  {
    printf("FAIL: getvmstats(after touch) returned %d\n", rc);
    exit(1);
  }

  if (!all_non_negative(&after))
  {
    printf("FAIL: negative counter in final stats\n");
    exit(1);
  }

  if (after.page_faults < before.page_faults)
  {
    printf("FAIL: page_faults decreased (%d -> %d)\n", before.page_faults, after.page_faults);
    exit(1);
  }

  if (getvmstats(-1, &after) >= 0)
  {
    printf("FAIL: invalid pid should fail\n");
    exit(1);
  }

  printf("PASS: getvmstats works\n");
  printf("before: pf=%d ev=%d in=%d out=%d res=%d\n",
         before.page_faults,
         before.pages_evicted,
         before.pages_swapped_in,
         before.pages_swapped_out,
         before.resident_pages);
  printf("after : pf=%d ev=%d in=%d out=%d res=%d\n",
         after.page_faults,
         after.pages_evicted,
         after.pages_swapped_in,
         after.pages_swapped_out,
         after.resident_pages);

  exit(0);
}
