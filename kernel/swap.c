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
uint16 RAID_MODE = RAID0;
int failed_disk = -1;

uint get_physical_block(int disk_id, uint block_offset)
{
    if (disk_id < 0 || disk_id >= NDISKS)
    {
        panic("get_physical_block: invalid disk_id");
    }

    if (block_offset >= DISKSIZE)
    {
        panic("get_physical_block: block_offset exceeds virtual disk capacity");
    }

    uint physical_block = sb.swapstart + (disk_id * DISKSIZE) + block_offset;

    return physical_block;
}

void set_raid_mode(int mode)
{
    RAID_MODE = mode;
    printf("RAID mode set to %d\n", mode);
}

void set_failed_disk(int disk)
{
    failed_disk = disk;
    printf("Failed disk set to %d\n", disk);
}

void sbzero(uint dev, uint logical_block)
{
    uint phys_block;

    if (RAID_MODE == RAID0)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        // Map to physical disk sector using your helper
        phys_block = get_physical_block(disk_id, block_offset);
    }
    else
    {
        panic("raid_bwrite: unsupported RAID mode");
    }

    struct buf *bp = bread(dev, phys_block);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
}

int sballoc(uint dev, int *blocks)
{
    int b, bi, m;
    struct buf *bp;
    int found = 0;

    for (b = 0; b < sb.swsize && found < 4; b += BPB)
    {
        bp = bread(dev, SSBITMAP(b, sb));
        for (bi = 0; bi < BPB && b + bi < sb.swsize && found < 4; bi++)
        {
            m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0)
            {
                bp->data[bi / 8] |= m;
                // printf("sballoc: allocated swap block %d\n", b + bi);
                blocks[found] = b + bi;
                found++;
            }
        }
        bwrite(bp);
        brelse(bp);
    }

    if (found < 4)
    {
        printf("sballoc: out of swap blocks\n");
        for (int i = 0; i < found; i++) {
            struct buf *bp = bread(dev, SSBITMAP(blocks[i], sb));
            int bi = blocks[i] % BPB;
            bp->data[bi / 8] &= ~(1 << (bi % 8));
            bwrite(bp);
            brelse(bp);
        }
        return -1;
    }

    for (int i = 0; i < 4; i++)
    {
        sbzero(dev, blocks[i]);
    }

    return 0;
}

void sbfree(uint dev, int *blocks)
{
    struct buf *bp;
    int bi, m;
    uint b;

    for (int i = 0; i < 4; i++)
    {
        if (blocks[i] == (uint)-1)
            continue;

        b = blocks[i];
        bp = bread(dev, SSBITMAP(b, sb));
        bi = b % BPB;
        m = 1 << (bi % 8);
        if ((bp->data[bi / 8] & m) == 0)
        {
            panic("freeing free swap block");
        }
        bp->data[bi / 8] &= ~m;

        bwrite(bp);
        brelse(bp);
    }
}
void sread(uint dev, uint logical_block, char *data)
{
    uint phys_block;

    if (RAID_MODE == RAID0)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        phys_block = get_physical_block(disk_id, block_offset);
    }
    else
    {
        panic("raid_bread: unsupported RAID mode");
    }

    // Standard xv6 buffer cache read sequence
    struct buf *b = bread(dev, phys_block);
    memmove(data, b->data, BSIZE);
    brelse(b);
}

void swrite(uint dev, uint logical_block, char *data)
{
    uint phys_block;

    if (RAID_MODE == RAID0)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        // Map to physical disk sector using your helper
        phys_block = get_physical_block(disk_id, block_offset);
    }
    else
    {
        panic("raid_bwrite: unsupported RAID mode");
    }

    struct buf *b = bread(dev, phys_block);
    memmove(b->data, data, BSIZE);
    bwrite(b);
    brelse(b);
}
