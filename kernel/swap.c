#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "swap.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

extern struct superblock sb;

void sbzero(uint dev, uint b)
{
    struct buf *bp = bread(dev, b+sb.swapstart);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
}

int sballoc(uint dev){
    int b, bi, m;
    struct buf *bp;
    
    bp = 0;
    for(b = 0; b < sb.swsize; b += BPB){
        bp = bread(dev, SSBLOCk(b, sb));
        for(bi = 0; bi < BPB && b + bi < sb.swsize; bi++){
        m = 1 << (bi % 8);
        if((bp->data[bi/8] & m) == 0){  // Is block free?
            bp->data[bi/8] |= m;  // Mark block in use.
            bwrite(bp);
            brelse(bp);
            sbzero(dev, b + bi); // Zero the block before returning it
            return sb.swapstart + b + bi;
        }
        }
        brelse(bp);
    }
    printf("sballoc: out of swap blocks\n");
    return -1;
}

void sbfree(uint dev, uint b){
    struct buf *bp;
    int bi, m;
    b-= sb.swapstart;
    bp = bread(dev, SSBLOCk(b, sb));
    bi = b % BPB;
    m = 1 << (bi % 8);
    if((bp->data[bi/8] & m) == 0){
        panic("freeing free swap block");
    }
    bp->data[bi/8] &= ~m;
    bwrite(bp);
    brelse(bp);
}
