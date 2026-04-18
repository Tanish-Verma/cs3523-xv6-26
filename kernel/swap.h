#ifndef SWAP_H
#define SWAP_H

#define SSBLOCk(b, sb) ((b)/BPB + sb.bmapswapstart) // Block of free swap space map containing bit for block b

struct swap_entry
{
    int in_use;
    pagetable_t pagetable;
    uint64 va;
    int blocks[4];
};


#define NSWAPDISK 4

#endif