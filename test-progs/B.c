#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"

int main() {
  printf("\n[TEST] Syscall-Heavy Retention Test\n");

  int pid = fork();

  if(pid == 0) {
    // Make many syscalls to demonstrate I/O behavior
    for(int i = 0; i < 20000; i++)
      getpid();
    exit(0);
  }

  // Let process run syscalls
  pause(100);

  struct mlfqinfo info;
  if(getmlfqinfo(pid, &info) < 0) {
    printf("FAIL: getmlfqinfo failed\n");
    kill(pid);
    wait(0);
    exit(0);
  }

  printf("Final Level: %d\n", info.level);
  printf("Syscalls: %d, Ticks at each level: %d %d %d %d\n",
         info.total_syscalls,
         info.ticks[0], info.ticks[1],
         info.ticks[2], info.ticks[3]);

  // Syscall-heavy should stay in queue 0 or 1
  if(info.level <= 1 && info.total_syscalls >= 20000)
    printf("PASS: I/O-bound retained in high queue despite syscall load\n");
  else if(info.level <= 1)
    printf("FAIL: Not enough syscalls recorded\n");
  else
    printf("FAIL: Syscall-heavy process demoted too much (level=%d)\n", info.level);

  exit(0);
}