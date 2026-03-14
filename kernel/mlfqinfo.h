#ifndef MLFQINFO_H
#define MLFQINFO_H
#include "param.h"

struct mlfqinfo
{
  int level;           // current queue level
  int ticks[NQUEUE];        // total ticks consumed at each level
  int times_scheduled; // number of times the process has been scheduled
  int total_syscalls;  // total system calls made
};
#endif
