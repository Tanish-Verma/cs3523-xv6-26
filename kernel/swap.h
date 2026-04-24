#ifndef SWAP_H
#define SWAP_H


#define SSBITMAP(b, sb) ((b)/BPB + sb.bmapswapstart) // Block of free swap space bit map containing bit for block b

struct swap_entry
{
    int in_use;
    pagetable_t pagetable;
    uint64 va;
    int blocks[4];
    int swapped_out; // Flag to indicate if the page has been swapped out or is in the process of being swapped out
};


#define NSWAPDISK 4

#endif