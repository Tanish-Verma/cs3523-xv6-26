#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       128000  // size of file system in blocks
#define SWAPSIZE     120000  // size of the swap space in blocks
#define MAXPATH      256   // maximum file path name
#define USERSTACK    1     // user stack pages
#define NQUEUE       4     // number of queues in MLFQ
#define FCFS         0     // first come first serve disk scheduling
#define SSTF         1     // shortest seek time first disk scheduling
#define ROTATIONAL_DELAY 7 // rotational delay
#define RAID0        0
#define RAID1        1
#define RAID5        5
#define NDISKS      4
#define DISKSIZE    SWAPSIZE / NDISKS