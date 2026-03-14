#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"

int main() {
  printf("\n[TEST] CPU-bound Demotion Test\n");

  int pid = fork();

  if(pid == 0) {
    volatile unsigned long i;
    // Long CPU work to ensure demotion
    for(i = 0; i < 1000000000UL; i++);
    exit(0);
  }

  // Sample at different time points to verify progressive demotion
  struct mlfqinfo info;
  int prev_level = 0;
  int reached_level_3 = 0;

  for(int sample = 1; sample <= 5; sample++) {
    pause(25);
    if(getmlfqinfo(pid, &info) < 0) {
      printf("FAIL: getmlfqinfo failed\n");
      kill(pid);
      wait(0);
      exit(0);
    }
    printf("Sample %d: level=%d, ticks[0]=%d, ticks[3]=%d\n",
           sample, info.level, info.ticks[0], info.ticks[3]);
    if(info.level >= prev_level)
      prev_level = info.level;
    if(info.level == 3)
      reached_level_3 = 1;
  }

  kill(pid);
  wait(0);

  if(reached_level_3 && info.ticks[3] > 0)
    printf("PASS: CPU-bound reached level 3 with ticks consumed\n");
  else
    printf("FAIL: CPU-bound did not demote to level 3\n");

  exit(0);
}