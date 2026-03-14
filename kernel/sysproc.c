#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mlfqinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0)
  {
    if (growproc(n) < 0)
    {
      return -1;
    }
  }
  else
  {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (killed(myproc()))
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_hello(void)
{
  printf("Hello from the kernel!\n");
  return 0;
}

uint64
sys_getpid2(void)
{
  uint64 pid = myproc()->pid;
  return pid;
}

// I tried defining a new lock,
// and understood that I must not reinitialize a lock after it
// has been initialized in initproc
extern struct spinlock wait_lock;

uint64
sys_getppid(void)
{
  struct proc *chproc = myproc();
  uint64 ppid = -1;
  acquire(&wait_lock);
  if (chproc->parent)
  {
    ppid = chproc->parent->pid;
  }
  release(&wait_lock);
  return ppid;
}

extern struct proc proc[NPROC];

uint64
sys_getnumchild(void)
{
  struct proc *pp = myproc();
  struct proc *p;
  uint64 numChild = 0;
  acquire(&wait_lock);
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != pp)
    {
      acquire(&p->lock);
      if (p->parent == pp && (p->state != ZOMBIE && p->state != UNUSED))
      {
        numChild++;
      }
      release(&p->lock);
    }
  }
  release(&wait_lock);
  return numChild;
}

uint64
sys_getsyscount(void)
{
  struct proc *p = myproc();
  int syscount;
  acquire(&p->lock);
  syscount = p->syscount;
  release(&p->lock);
  return syscount;
}

uint64
sys_getchildsyscount(void)
{
  int pid, syscount = -1;
  argint(0, &pid);
  if (pid <= 0)
  {
    return -1;
  }

  struct proc *p;
  struct proc *pp = myproc();
  acquire(&wait_lock);
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != pp)
    {
      acquire(&p->lock);
      if (p->parent == pp && p->pid == pid && p->state != UNUSED)
      {
        syscount = p->syscount;
        release(&p->lock);
        break;
      }
      release(&p->lock);
    }
  }
  release(&wait_lock);
  return syscount;
}

uint64
sys_getlevel(void)
{
  struct proc *p = myproc();
  int level;
  acquire(&p->lock);
  level = p->queue_level;
  release(&p->lock);
  return level;
}

uint64
sys_getmlfqinfo(void)
{
  int pid;
  uint64 uaddr;
  argint(0, &pid);
  argaddr(1, &uaddr);
  if (pid <= 0)
  {
    printf("Invalid PID: %d\n", pid);
    return -1;
  }
  struct mlfqinfo info = {0};
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED)
    {
      info.level = p->queue_level;
      for (int i = 0; i < NQUEUE; i++)
      {
        info.ticks[i] = p->ticks_used[i];
      }
      info.times_scheduled = p->sched_count;
      info.total_syscalls = p->syscount;
      release(&p->lock);
      if (copyout(myproc()->pagetable, uaddr, (char *)&info, sizeof(info)) < 0)
      {
        return -1;
      }
      return 0;
    }
    release(&p->lock);
  }
  printf("Process with PID %d not found\n", pid);
  return -1;
}