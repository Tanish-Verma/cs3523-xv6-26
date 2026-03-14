// Saved registers for kernel context switches.
struct context
{
  uint64 ra; // Return Address
  uint64 sp; // Stack Pointer

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU core state.
struct cpu
{
  struct proc *proc;      // The process running on this cpu, or null.
  struct context context; // swtch() here to enter scheduler().
  int noff;               // Depth of push_off() nesting.
  int intena;             // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];
// There exists an array of per-CPU state,
//  one entry per hardware core.

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe
{
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter. User program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;            // return address
  /*  48 */ uint64 sp;            // stack pointer
  /*  56 */ uint64 gp;            // global pointer -> Store heap pointer
  /*  64 */ uint64 tp;            // Thread pointer
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate
{
  UNUSED,
  USED,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
}; // enumeration of processes

// Per-process state
// PCB for each process
struct proc
{
  struct spinlock lock;
  // If a process is being run by a cpu, it locks itself
  //  so that another cpu can't edit it

  // p->lock must be held when using these:
  enum procstate state;     // Process state
  void *chan;               // If non-zero, sleeping on chan
  int killed;               // If non-zero, have been killed
  int xstate;               // Exit status to be returned to parent's wait
  int pid;                  // Process ID
  int syscount;             // Number of system calls invoked
  int ticks[NQUEUE];        // Array to store ticks at each level
  int queue_level;          // Level of the queue the process is in
  int ticks_cnsum;          // Number of ticks consumed in the current queue
  int ticks_used[NQUEUE];   // Number of ticks used at each level
  int sched_count;          // Number of times scheduled
  int del_s;                // Temporary storage for computing del s
  int del_t;                // Temporary storage for computing del t
  int to_demote;            // Flag to indicate if process should be demoted on next tick
  // wait_lock must be held when using this:
  struct proc *parent; // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  // Trampoline uses the data stored in the trapframe to return
  // back to the original state
  struct context context;     // swtch() here to run process
  struct file *ofile[NOFILE]; // Open files
  struct inode *cwd;          // Current directory
  char name[16];              // Process name (debugging)
};

// User program
//    ↓ trap
// TRAMPOLINE (uservec)
//    ↓
// Save user registers → trapframe
// Switch to kernel stack
// Switch to kernel page table
//    ↓
// Kernel C code (kernel_trap)
//    ↓
// TRAMPOLINE (userret)
//    ↓
// Restore user registers
// Switch to user page table
// Return to user code