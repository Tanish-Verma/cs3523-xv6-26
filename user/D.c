#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"
#include "kernel/param.h"

enum { BOOST_TIME = 128 };

void interactive() {
  while(1)
    getpid();
}

int main() {
  printf("\n[TEST] Starvation Prevention\n");

  int interactive_pids[5];

  // Spawn 5 interactive processes to heavily compete with CPU-bound
  for(int i = 0; i < 5; i++) {
    int pid = fork();
    if(pid == 0) {
      interactive();
      exit(0);
    }
    interactive_pids[i] = pid;
  }

  // Spawn CPU-bound child
  int cpu = fork();
  if(cpu == 0) {
    volatile unsigned long i;
    // Run long enough to consume significant CPU time
    for(i = 0; i < 300000000UL; i++);
    exit(0);
  }

  // Phase 1: Wait for CPU-bound to be demoted
  printf("Phase 1: Waiting for CPU-bound to be demoted...\n");
  struct mlfqinfo info;
  int was_demoted = 0;

  for(int i = 0; i < 100; i++) {
    if(getmlfqinfo(cpu, &info) == 0 && info.level > 0) {
      was_demoted = 1;
      printf("CPU-bound demoted to level %d\n", info.level);
      break;
    }
    pause(2);
  }

  if(!was_demoted) {
    printf("FAIL: CPU-bound was never demoted despite interactive load\n");
    for(int i = 0; i < 5; i++)
      kill(interactive_pids[i]);
    kill(cpu);
    for(int i = 0; i < 5; i++)
      wait(0);
    wait(0);
    exit(0);
  }

  // Phase 2: Synchronize to next boost boundary and check for promotion to level 0
  printf("Phase 2: Waiting for boost to promote back to level 0...\n");
  int boosted = 0;

  for(int attempt = 0; attempt < 5 && !boosted; attempt++) {
    int now = uptime();
    int wait = BOOST_TIME - (now % BOOST_TIME);
    if(wait == BOOST_TIME)
      wait = 1;
    pause(wait);

    // Sample immediately after boost for several ticks
    for(int sample = 0; sample < 10 && !boosted; sample++) {
      if(getmlfqinfo(cpu, &info) == 0 && info.level == 0) {
        boosted = 1;
        printf("CPU-bound promoted to level 0 after boost\n");
        break;
      }
      pause(1);
    }
  }

  if(!boosted) {
    printf("FAIL: CPU-bound not promoted to level 0 after boost window\n");
    for(int i = 0; i < 5; i++)
      kill(interactive_pids[i]);
    kill(cpu);
    for(int i = 0; i < 5; i++)
      wait(0);
    wait(0);
    exit(0);
  }

  // Phase 3: Verify CPU-bound completes despite interactive competition
  printf("Phase 3: Verifying CPU-bound completes (runs after boost)...\n");
  int cpu_finished = 0;
  int timeout = 60;

  for(int elapsed = 0; elapsed < timeout; elapsed++) {
    int result = wait(0);
    if(result == cpu) {
      cpu_finished = 1;
      printf("CPU-bound completed\n");
      break;
    }
    pause(1);
  }

  // Clean up
  for(int i = 0; i < 5; i++)
    kill(interactive_pids[i]);
  if(!cpu_finished)
    kill(cpu);

  for(int i = 0; i < 5; i++)
    wait(0);
  if(!cpu_finished)
    wait(0);

  if(boosted && cpu_finished) {
    printf("PASS: CPU-bound was demoted, boosted to level 0, and completed (no starvation)\n");
  } else {
    printf("FAIL: CPU-bound did not complete after boost\n");
  }

  exit(0);
}