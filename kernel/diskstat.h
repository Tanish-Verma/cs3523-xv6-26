struct diskstats
{
    int reads;          // Total number of disk reads
    int writes;         // Total number of disk writes
    int avg_latency;  // Average latency of all disk operations (in ticks)
};
