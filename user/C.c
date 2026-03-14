#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"

int main() {
  printf("\n[TEST] Mixed Workload Priority Test\n");

  int start = uptime();

  int cpu = fork();
  if(cpu == 0) {
    volatile unsigned long i;
    for(i = 0; i < 900000000UL; i++);
    exit(0);
  }

  int interactive = fork();
  if(interactive == 0) {
    for(int i = 0; i < 20000; i++)
      getpid();
    exit(0);
  }

  // Track finish order and queue levels
  int first_finished = -1;
  int wstatus;

  for(int i = 0; i < 2; i++) {
    int result = wait(&wstatus);
    if(result == interactive && first_finished == -1) {
      first_finished = interactive;
      printf("Interactive finished first\n");
    } else if(result == cpu && first_finished == -1) {
      first_finished = cpu;
      printf("CPU-bound finished first\n");
    }
  }

  int elapsed = uptime() - start;

  // Rigorous check: interactive should finish before CPU-bound
  if(first_finished == interactive) {
    printf("PASS: Interactive finished before CPU-bound (elapsed %d ticks)\n", elapsed);
  } else {
    printf("FAIL: CPU-bound finished before or with interactive\n");
  }

  exit(0);
}