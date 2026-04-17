#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"
#include "kernel/param.h"

enum { NCHILD = NCPU + 4 };
enum { BOOST_TIME = 128 };

static void
cpu_burn(void)
{
  volatile unsigned long i = 0;
  while (1) {
    // Pure CPU work so workers naturally get demoted under MLFQ.
    i++;
  }
}

int
main(void)
{
  printf("\n[TEST] Global Boost Test\n");

  int pids[NCHILD];
  int was_demoted[NCHILD];
  struct mlfqinfo info;

  for (int i = 0; i < NCHILD; i++) {
    was_demoted[i] = 0;
    int pid = fork();
    if (pid < 0) {
      printf("FAIL: fork failed\n");
      exit(0);
    }
    if (pid == 0) {
      cpu_burn();
      exit(0);
    }
    pids[i] = pid;
  }

  // Let workers run long enough to get out of queue 0.
  pause(70);

  for (int i = 0; i < NCHILD; i++) {
    if (getmlfqinfo(pids[i], &info) == 0 && info.level > 0) {
      was_demoted[i] = 1;
    }
  }

  int saw_demoted = 0;
  for (int i = 0; i < NCHILD; i++) {
    if (was_demoted[i]) {
      saw_demoted = 1;
      break;
    }
  }

  if (!saw_demoted) {
    printf("FAIL: did not observe any demoted worker before boost window\n");
    for (int i = 0; i < NCHILD; i++)
      kill(pids[i]);
    for (int i = 0; i < NCHILD; i++)
      wait(0);
    exit(0);
  }

  int boosted = 0;
  // For RUNNABLE-only boosting, sample immediately after boost boundaries.
  // This assumes BOOST_TIME in kernel/trap.c is 128.
  for (int attempt = 0; attempt < 4 && !boosted; attempt++) {
    int now = uptime();
    int wait = BOOST_TIME - (now % BOOST_TIME);
    if (wait == BOOST_TIME)
      wait = 1;
    pause(wait);

    for (int sample = 0; sample < 8 && !boosted; sample++) {
      for (int i = 0; i < NCHILD; i++) {
        if (!was_demoted[i])
          continue;
        if (getmlfqinfo(pids[i], &info) == 0 && info.level == 0) {
          boosted = 1;
          break;
        }
      }
      if (!boosted)
        pause(1);
    }
  }

  if (boosted)
    printf("PASS: observed demoted worker boosted back to level 0\n");
  else {
    printf("FAIL: no demoted worker observed at level 0 after boost window\n");
    for (int i = 0; i < NCHILD; i++) {
      if (getmlfqinfo(pids[i], &info) == 0)
        printf("pid %d final level %d\n", pids[i], info.level);
    }
  }

  for (int i = 0; i < NCHILD; i++)
    kill(pids[i]);
  for (int i = 0; i < NCHILD; i++)
    wait(0);

  exit(0);
}