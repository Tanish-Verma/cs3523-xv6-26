#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mlfqinfo.h"

// CPU-bound process: minimal syscalls, lots of computation
void cpu_bound_process()
{
    int pid = getpid();
    // printf("CPU-bound process (PID %d) started\n", pid);
    
    // Burn CPU for a long time with TRULY minimal syscalls
    // This ensures del_t >> del_s
    volatile long x = 0;
    
    // Long CPU-intensive loop that will span many timer interrupts
    // and multiple time quanta, forcing demotion
    // NOTE: We minimize printf to reduce syscalls (printf makes ~50 syscalls per call!)
    for (long i = 0; i < 1000000000; i++)
    {
        x += i;
        x = x * 7 + 13;
        x = x % 1000000;
        
    }
    
    struct mlfqinfo final_info = {0};
    if (getmlfqinfo(pid, &final_info) == 0)
    {
        printf("[CPU-bound PID %d] FINAL: Level=%d (DEMOTED to 2/3)\n", 
               pid, final_info.level);
        printf("  Syscalls=%d Ticks: Q0=%d Q1=%d Q2=%d Q3=%d\n",
               final_info.total_syscalls,
               final_info.ticks[0], final_info.ticks[1], 
               final_info.ticks[2], final_info.ticks[3]);
    }
    
    exit(0);
}

// Interactive process: many syscalls per tick
void interactive_process()
{
    int pid = getpid();
    printf("Interactive process (PID %d) started\n", pid);
    
    for (int iter = 0; iter < 15; iter++)
    {
        // Do lots of syscalls continuously
        // This ensures del_s >> del_t
        for (int i = 0; i < 100000; i++)
        {
            getpid(); // Increment del_s with minimal CPU consumption
        }
        
        // Check our level occasionally
        if (iter % 3 == 0)
        {
            struct mlfqinfo info = {0};
            if (getmlfqinfo(pid, &info) == 0)
            {
                printf("[Interactive PID %d] Check %d: Level=%d, Syscalls=%d\n", 
                       pid, iter / 3, info.level, info.total_syscalls);
            }
        }
    }
    
    struct mlfqinfo final_info = {0};
    if (getmlfqinfo(pid, &final_info) == 0)
    {
        printf("[Interactive PID %d] FINAL: Level=%d (should stay at 0 or 1)\n", 
               pid, final_info.level);
        printf("  Ticks per queue: Q0=%d Q1=%d Q2=%d Q3=%d\n",
               final_info.ticks[0], final_info.ticks[1], 
               final_info.ticks[2], final_info.ticks[3]);
    }
    
    exit(0);
}

// Sleep-heavy process: tests counter reset on sleep()
void sleepy_interactive_process()
{
    int pid = getpid();
    printf("Sleepy-interactive process (PID %d) started\n", pid);
    
    for (int iter = 0; iter < 10; iter++)
    {
        // Do many syscalls before sleeping
        for (int i = 0; i < 50000; i++)
        {
            getpid();
        }
        
        // Sleep (should reset counters and preserve interactive status)
        pause(20);
        
        if (iter % 2 == 0)
        {
            struct mlfqinfo info = {0};
            if (getmlfqinfo(pid, &info) == 0)
            {
                printf("[Sleepy PID %d] After sleep %d: Level=%d, Syscalls=%d\n", 
                       pid, iter, info.level, info.total_syscalls);
            }
        }
    }
    
    struct mlfqinfo final_info = {0};
    if (getmlfqinfo(pid, &final_info) == 0)
    {
        printf("[Sleepy PID %d] FINAL: Level=%d (should stay at 0 or 1)\n", 
               pid, final_info.level);
        printf("  Ticks per queue: Q0=%d Q1=%d Q2=%d Q3=%d\n",
               final_info.ticks[0], final_info.ticks[1], 
               final_info.ticks[2], final_info.ticks[3]);
    }
    
    exit(0);
}

int main()
{
    printf("=== MLFQ Demotion Test ===\n");
    printf("Testing system-call-aware demotion rule:\n");
    printf("  If del_s >= del_t: interactive → no demotion\n");
    printf("  If del_s < del_t: CPU-bound → demotion\n\n");
    
    // Create CPU-bound process
    int cpu_pid = fork();
    if (cpu_pid == 0)
    {
        cpu_bound_process();
    }
    
    // Create interactive process
    int inter_pid = fork();
    if (inter_pid == 0)
    {
        interactive_process();
    }
    
    // Create sleepy-interactive process
    int sleepy_pid = fork();
    if (sleepy_pid == 0)
    {
        sleepy_interactive_process();
    }
    
    // Wait for all children
    wait(0);
    wait(0);
    wait(0);
    
    printf("\n=== Test Summary ===\n");
    printf("Expected results:\n");
    printf("  1. CPU-bound process: demoted to level 2 or 3\n");
    printf("  2. Interactive process: stays at level 0 or 1\n");
    printf("  3. Sleepy process: stays at level 0 or 1 (counters reset on sleep)\n");
    printf("===================\n");
    
    exit(0);
}
