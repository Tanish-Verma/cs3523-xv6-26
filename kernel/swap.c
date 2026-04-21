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
uint16 RAID_MODE = RAID5;
int failed_disk = -1;

uint get_physical_block(int disk_id, uint block_offset)
{
    if (disk_id < 0 || disk_id >= NDISKS)
        panic("get_physical_block: invalid disk_id");

    if (block_offset >= DISKSIZE)
        panic("get_physical_block: block_offset exceeds virtual disk capacity");

    return sb.swapstart + (disk_id * DISKSIZE) + block_offset;
}

void set_raid_mode(int mode)
{
    int old_mode = RAID_MODE;
    RAID_MODE = mode;
    printf("RAID mode set to %d\n", mode);

    if (mode == RAID5 && old_mode != RAID5)
    {
        for (uint offset = 0; offset < DISKSIZE; offset++)
        {
            uint parity_disk = offset % NDISKS; // Parity disk for this stripe
            char *new_parity = (char *)kalloc();
            if (!new_parity) panic("set_raid_mode: OOM");
            memset(new_parity, 0, BSIZE);

            // XOR all data disks in the stripe
            for (int d = 0; d < NDISKS; d++)
            {
                if (d == parity_disk) continue;
                
                uint phys = get_physical_block(d, offset);
                struct buf *b = bread(ROOTDEV, phys); 
                for (int j = 0; j < BSIZE; j++)
                    new_parity[j] ^= b->data[j];
                brelse(b);
            }

            // Write the valid parity to the parity disk
            uint phys_parity = get_physical_block(parity_disk, offset);
            struct buf *pb = bread(ROOTDEV, phys_parity);
            memmove(pb->data, new_parity, BSIZE);
            bwrite(pb);
            brelse(pb);
            kfree(new_parity);
        }
    }
}

void set_failed_disk(int disk)
{
    int old_failed = failed_disk;
    failed_disk = disk;
    printf("Failed disk set to %d\n", disk);

    // If we are repairing a disk (switching the failure to a different disk)
    if (RAID_MODE == RAID5 && old_failed != -1 && old_failed != disk)
    {
        // Rebuild the previously failed disk to clear stale data/parity
        for (uint offset = 0; offset < DISKSIZE; offset++)
        {
            char * reconstructed = (char *)kalloc();
            memset(reconstructed, 0, BSIZE);

            // Reconstruct from all surviving disks
            for (int d = 0; d < NDISKS; d++)
            {
                if (d == old_failed) continue;
                
                uint phys = get_physical_block(d, offset);
                struct buf *b = bread(ROOTDEV, phys); // Usually ROOTDEV or 1
                for (int j = 0; j < BSIZE; j++)
                    reconstructed[j] ^= b->data[j];
                brelse(b);
            }
            // printf("Rebuilt block offset %d for repaired disk %d\n", offset, old_failed);
            // Write the rebuilt data back to the repaired disk
            uint phys_rebuild = get_physical_block(old_failed, offset);
            struct buf *b = bread(ROOTDEV, phys_rebuild);
            memmove(b->data, reconstructed, BSIZE);
            bwrite(b);
            brelse(b);

            kfree(reconstructed);
        }
    }
}

// Get the mirror logical block for RAID 1.
// Logical blocks are striped across disks:
//   logical 0 -> disk 0, logical 1 -> disk 1, ..., logical N -> disk 0, ...
// So: disk_id = logical % NDISKS, block_offset = logical / NDISKS
// Mirror disk is (disk_id + 1) % NDISKS
// Mirror logical block = mirror_disk_id + block_offset * NDISKS
uint get_raid1_mirror_block(uint logical_block)
{
    int disk_id = logical_block % NDISKS;
    uint block_offset = logical_block / NDISKS;

    int mirror_disk_id = (disk_id + 1) % NDISKS;

    // Reconstruct logical block index for the mirror
    return block_offset * NDISKS + mirror_disk_id;
}

uint get_raid5_parity_disk(uint logical_block)
{
    int stripe_number = logical_block / (NDISKS);
    return stripe_number % NDISKS;
}

void sbzero(uint dev, uint logical_block)
{
    if (RAID_MODE == RAID0 || RAID_MODE == RAID5)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        uint phys_block = get_physical_block(disk_id, block_offset);
        struct buf *bp = bread(dev, phys_block);
        memset(bp->data, 0, BSIZE);
        bwrite(bp);
        brelse(bp);
    }
    else if (RAID_MODE == RAID1)
    {
        int primary_disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        uint mirror_logical = get_raid1_mirror_block(logical_block);
        int mirror_disk_id = mirror_logical % NDISKS;
        uint mirror_offset = mirror_logical / NDISKS;

        uint primary_block = get_physical_block(primary_disk_id, block_offset);
        uint mirror_block = get_physical_block(mirror_disk_id, mirror_offset);

        if (primary_disk_id != failed_disk)
        {
            struct buf *bp = bread(dev, primary_block);
            memset(bp->data, 0, BSIZE);
            bwrite(bp);
            brelse(bp);
        }

        if (mirror_disk_id != failed_disk)
        {
            struct buf *bp = bread(dev, mirror_block);
            memset(bp->data, 0, BSIZE);
            bwrite(bp);
            brelse(bp);
        }
    }
    else
    {
        panic("sbzero: unsupported RAID mode");
    }
}
int sballoc(uint dev, int *blocks)
{
    int b, bi, m;
    struct buf *bp;
    int found = 0;

    if (RAID_MODE == RAID0)
    {
        for (b = 0; b < sb.swsize && found < 4; b += BPB)
        {
            bp = bread(dev, SSBITMAP(b, sb));
            for (bi = 0; bi < BPB && b + bi < sb.swsize && found < 4; bi++)
            {
                m = 1 << (bi % 8);
                if ((bp->data[bi / 8] & m) == 0 && (b + bi) % NDISKS != failed_disk)
                {
                    bp->data[bi / 8] |= m;
                    blocks[found] = b + bi;
                    found++;
                }
            }
            bwrite(bp);
            brelse(bp);
        }
    }
    else if (RAID_MODE == RAID1)
    {
        for (b = 0; b < sb.swsize && found < 4; b += BPB)
        {
            struct buf *bp = bread(dev, SSBITMAP(b, sb));
            for (bi = 0; bi < BPB && b + bi < sb.swsize && found < 4; bi++)
            {
                uint primary_logical = b + bi;
                uint mirror_logical = get_raid1_mirror_block(primary_logical);

                // 1. Check if primary is free and disks haven't failed
                if ((bp->data[bi / 8] & (1 << (bi % 8))) == 0 &&
                    (primary_logical % NDISKS) != failed_disk &&
                    mirror_logical < (uint)sb.swsize &&
                    (mirror_logical % NDISKS) != failed_disk)
                {
                    // 2. Check if mirror is ALSO free
                    int mbi = mirror_logical % BPB;
                    uint mirror_bmap = SSBITMAP(mirror_logical, sb);
                    int mirror_is_free = 0;

                    if (mirror_bmap == SSBITMAP(primary_logical, sb))
                    {
                        // Mirror is in the same buffer we already hold
                        mirror_is_free = ((bp->data[mbi / 8] & (1 << (mbi % 8))) == 0);
                    }
                    else
                    {
                        // Mirror is in a different buffer; read it temporarily to check
                        struct buf *mbp = bread(dev, mirror_bmap);
                        mirror_is_free = ((mbp->data[mbi / 8] & (1 << (mbi % 8))) == 0);
                        brelse(mbp);
                    }

                    // 3. Only proceed if BOTH are free
                    if (mirror_is_free)
                    {
                        // Mark primary
                        bp->data[bi / 8] |= (1 << (bi % 8));

                        // Mark mirror
                        if (mirror_bmap == SSBITMAP(primary_logical, sb))
                        {
                            bp->data[mbi / 8] |= (1 << (mbi % 8));
                        }
                        else
                        {
                            struct buf *mbp = bread(dev, mirror_bmap);
                            mbp->data[mbi / 8] |= (1 << (mbi % 8));
                            bwrite(mbp);
                            brelse(mbp);
                        }

                        blocks[found++] = primary_logical;
                    }
                }
            }
            bwrite(bp);
            brelse(bp);
        }
    }
    else if (RAID_MODE == RAID5)
    {
        for (b = 0; b < sb.swsize && found < 4; b += BPB)
        {
            bp = bread(dev, SSBITMAP(b, sb));
            for (bi = 0; bi < BPB && b + bi < sb.swsize && found < 4; bi++)
            {
                uint logical = b + bi;
                uint stripe = logical / NDISKS;
                uint parity_disk = get_raid5_parity_disk(logical);
                uint parity_block = stripe * NDISKS + parity_disk;

                // Skip if this block is the parity slot for its stripe
                if (logical == parity_block)
                    continue;

                // Skip if this block's disk has failed
                if ((int)(logical % NDISKS) == failed_disk)
                    continue;

                m = 1 << (bi % 8);
                if ((bp->data[bi / 8] & m) == 0)
                {
                    bp->data[bi / 8] |= m;
                    blocks[found++] = logical;
                }
            }
            bwrite(bp);
            brelse(bp);
        }
    }
    else
    {
        panic("sballoc: unsupported RAID mode");
    }

    if (found < 4)
    {
        printf("sballoc: out of swap blocks\n");
        for (int i = 0; i < found; i++)
        {
            bp = bread(dev, SSBITMAP(blocks[i], sb));
            int bi2 = blocks[i] % BPB;
            bp->data[bi2 / 8] &= ~(1 << (bi2 % 8));
            bwrite(bp);
            brelse(bp);

            if (RAID_MODE == RAID1)
            {
                uint mirror_logical = get_raid1_mirror_block(blocks[i]);
                uint primary_bmap = SSBITMAP(blocks[i], sb);
                uint mirror_bmap = SSBITMAP(mirror_logical, sb);
                int mbi2 = mirror_logical % BPB;

                if (primary_bmap == mirror_bmap)
                {
                    // Already freed primary above in same block — just clear mirror bit
                    bp = bread(dev, mirror_bmap);
                    bp->data[mbi2 / 8] &= ~(1 << (mbi2 % 8));
                    bwrite(bp);
                    brelse(bp);
                }
                else
                {
                    bp = bread(dev, mirror_bmap);
                    bp->data[mbi2 / 8] &= ~(1 << (mbi2 % 8));
                    bwrite(bp);
                    brelse(bp);
                }
            }
        }
        return -1;
    }

    for (int i = 0; i < found; i++)
    {
        if (RAID_MODE == RAID0 || RAID_MODE == RAID1)
        {
            sbzero(dev, blocks[i]);
        }
        else if (RAID_MODE == RAID5)
        {

            // char old_data[BSIZE];
            char *old_data = (char *)kalloc();
            uint phys_block = get_physical_block(blocks[i] % NDISKS, blocks[i] / NDISKS);
            struct buf *dbp = bread(dev, phys_block);
            memmove(old_data, dbp->data, BSIZE);
            brelse(dbp);

            sbzero(dev, blocks[i]);
            uint logical = blocks[i];
            uint stripe = logical / NDISKS;
            uint parity_disk = get_raid5_parity_disk(logical);
            uint offset = stripe; // one block per stripe per disk

            if ((int)parity_disk == failed_disk){
                kfree(old_data);
                continue; // can't update parity, skip
            }

            // Read old parity
            uint parity_phys = get_physical_block(parity_disk, offset);
            struct buf *pbp = bread(dev, parity_phys);
            char *old_parity = (char *)kalloc();
            memmove(old_parity, pbp->data, BSIZE);
            brelse(pbp);

            // new_parity = old_parity XOR old_data
            char *new_parity = (char *)kalloc();
            for (int j = 0; j < BSIZE; j++)
                new_parity[j] = old_parity[j] ^ old_data[j];

            // Write new parity
            pbp = bread(dev, parity_phys);
            memmove(pbp->data, new_parity, BSIZE);
            bwrite(pbp);
            brelse(pbp);

            kfree(old_data);
            kfree(old_parity);
            kfree(new_parity);
        }
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
        if (blocks[i] == -1)
            continue;

        b = blocks[i];

        if (RAID_MODE == RAID0 || RAID_MODE == RAID5)
        {
            bp = bread(dev, SSBITMAP(b, sb));
            bi = b % BPB;
            m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0)
                panic("freeing free swap block");
            bp->data[bi / 8] &= ~m;
            bwrite(bp);
            brelse(bp);
        }
        else if (RAID_MODE == RAID1)
        {
            uint mirror_logical = get_raid1_mirror_block(b);
            uint primary_bmap = SSBITMAP(b, sb);
            uint mirror_bmap = SSBITMAP(mirror_logical, sb);
            int pbi = b % BPB;
            int mbi = mirror_logical % BPB;
            int pm = 1 << (pbi % 8);
            int mm = 1 << (mbi % 8);

            if (primary_bmap == mirror_bmap)
            {
                // Handle both in one buffer
                bp = bread(dev, primary_bmap);
                if ((bp->data[pbi / 8] & pm) == 0)
                    panic("freeing free swap block");
                if ((bp->data[mbi / 8] & mm) == 0)
                    panic("freeing free RAID1 mirror block");
                bp->data[pbi / 8] &= ~pm;
                bp->data[mbi / 8] &= ~mm;
                bwrite(bp);
                brelse(bp);
            }
            else
            {
                bp = bread(dev, primary_bmap);
                if ((bp->data[pbi / 8] & pm) == 0)
                    panic("freeing free swap block");
                bp->data[pbi / 8] &= ~pm;
                bwrite(bp);
                brelse(bp);

                struct buf *mbp = bread(dev, mirror_bmap);
                if ((mbp->data[mbi / 8] & mm) == 0)
                    panic("freeing free RAID1 mirror block");
                mbp->data[mbi / 8] &= ~mm;
                bwrite(mbp);
                brelse(mbp);
            }
        }
    }
}

extern int disk_head;
void sread(uint dev, uint logical_block, char *data)
{
    if (RAID_MODE == RAID0)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;
        uint phys_block = get_physical_block(disk_id, block_offset);

        struct buf *b = bread(dev, phys_block);
        memmove(data, b->data, BSIZE);
        brelse(b);
    }
    else if (RAID_MODE == RAID1)
    {
        int primary_disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        uint mirror_logical = get_raid1_mirror_block(logical_block);
        int mirror_disk_id = mirror_logical % NDISKS;
        uint mirror_offset = mirror_logical / NDISKS;

        uint primary_block = get_physical_block(primary_disk_id, block_offset);
        uint mirror_block = get_physical_block(mirror_disk_id, mirror_offset);

        uint read_block;

        if (primary_disk_id == failed_disk && mirror_disk_id != failed_disk)
        {
            read_block = mirror_block;
        }
        else if (mirror_disk_id == failed_disk && primary_disk_id != failed_disk)
        {
            read_block = primary_block;
        }
        else if (primary_disk_id != failed_disk && mirror_disk_id != failed_disk)
        {
            uint dist = abs_diff(primary_block, disk_head);
            uint mirror_dist = abs_diff(mirror_block, disk_head);
            read_block = (mirror_dist < dist) ? mirror_block : primary_block;
        }
        else
        {
            panic("sread: both RAID1 copies have failed");
        }

        struct buf *b = bread(dev, read_block);
        memmove(data, b->data, BSIZE);
        brelse(b);
    }
    else if (RAID_MODE == RAID5)
    {
        uint stripe = logical_block / NDISKS;
        int data_disk = logical_block % NDISKS;
        uint offset = stripe; // one block per stripe per disk

        uint phys_block = get_physical_block(data_disk, offset);

        if (failed_disk != data_disk)
        {
            struct buf *b = bread(dev, phys_block);
            memmove(data, b->data, BSIZE);
            brelse(b);
        }
        else
        {
            // Target data disk failed — reconstruct by XOR-ing all surviving disks in the stripe
            memset(data, 0, BSIZE);

            for (int d = 0; d < NDISKS; d++)
            {
                if (d == failed_disk)
                    continue;

                uint phys = get_physical_block(d, offset);
                struct buf *b = bread(dev, phys);
                for (int j = 0; j < BSIZE; j++)
                    data[j] ^= b->data[j];
                brelse(b);
            }
        }
    }
    else
    {
        panic("sread: unsupported RAID mode");
    }
}

void swrite(uint dev, uint logical_block, char *data)
{
    if (RAID_MODE == RAID0)
    {
        int disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;
        uint phys_block = get_physical_block(disk_id, block_offset);

        struct buf *b = bread(dev, phys_block);
        memmove(b->data, data, BSIZE);
        bwrite(b);
        brelse(b);
    }
    else if (RAID_MODE == RAID1)
    {
        int primary_disk_id = logical_block % NDISKS;
        uint block_offset = logical_block / NDISKS;

        uint mirror_logical = get_raid1_mirror_block(logical_block);
        int mirror_disk_id = mirror_logical % NDISKS; // FIX: was (primary+2)%NDISKS
        uint mirror_offset = mirror_logical / NDISKS;

        uint primary_block = get_physical_block(primary_disk_id, block_offset);
        uint mirror_block = get_physical_block(mirror_disk_id, mirror_offset);

        if (primary_disk_id == failed_disk && mirror_disk_id == failed_disk)
            panic("swrite: both RAID1 copies have failed");

        if (primary_disk_id != failed_disk)
        {
            struct buf *b = bread(dev, primary_block);
            memmove(b->data, data, BSIZE);
            bwrite(b);
            brelse(b);
        }

        if (mirror_disk_id != failed_disk)
        {
            struct buf *b = bread(dev, mirror_block);
            memmove(b->data, data, BSIZE);
            bwrite(b);
            brelse(b);
        }
    }
    else if (RAID_MODE == RAID5)
    {
        uint stripe = logical_block / NDISKS;
        int data_disk = logical_block % NDISKS;
        uint parity_disk = get_raid5_parity_disk(logical_block);
        uint offset = stripe;
        char *old_data = (char *)kalloc();
        char *old_parity = (char *)kalloc();
        char *new_parity = (char *)kalloc();

        if (!old_data || !old_parity || !new_parity)
        {
            if (old_data)
                kfree(old_data);
            if (old_parity)
                kfree(old_parity);
            if (new_parity)
                kfree(new_parity);
            panic("swrite RAID5: kalloc failed");
        }

        if (offset >= DISKSIZE)
        {
            panic("swrite RAID5: stripe offset exceeds DISKSIZE");
        }
        if (data_disk == (int)parity_disk)
        {
            panic("swrite RAID5: logical block maps to parity disk");
        }

        if (data_disk == failed_disk && (int)parity_disk == failed_disk)
        {
            panic("swrite RAID5: both data and parity disks failed");
        }

        uint data_phys = get_physical_block(data_disk, offset);
        uint parity_phys = get_physical_block(parity_disk, offset);

        // Step 1: read old data
        if (data_disk != failed_disk)
        {
            struct buf *b = bread(dev, data_phys);
            memmove(old_data, b->data, BSIZE);
            brelse(b);
        }
        else
        {
            // Reconstruct old data from all surviving disks
            memset(old_data, 0, BSIZE);
            for (int d = 0; d < NDISKS; d++)
            {
                if (d == failed_disk)
                    continue;
                struct buf *b = bread(dev, get_physical_block(d, offset));
                for (int j = 0; j < BSIZE; j++)
                    old_data[j] ^= b->data[j];
                brelse(b);
            }
        }

        // Step 2: read old parity
        if ((int)parity_disk != failed_disk)
        {
            struct buf *b = bread(dev, parity_phys);
            memmove(old_parity, b->data, BSIZE);
            brelse(b);
        }
        else
        {
            // Reconstruct old parity from all surviving data disks
            memset(old_parity, 0, BSIZE);
            for (int d = 0; d < NDISKS; d++)
            {
                if (d == failed_disk)
                {
                    continue;
                }
                struct buf *b = bread(dev, get_physical_block(d, offset));
                for (int j = 0; j < BSIZE; j++)
                    old_parity[j] ^= b->data[j];
                brelse(b);
            }
        }

        // Step 3: compute new parity subtractively
        // new_parity = old_parity XOR old_data XOR new_data
        for (int j = 0; j < BSIZE; j++)
            new_parity[j] = old_parity[j] ^ old_data[j] ^ data[j];

        // Step 4: write new data (if data disk not failed)
        if (data_disk != failed_disk)
        {
            struct buf *b = bread(dev, data_phys);
            memmove(b->data, data, BSIZE);
            bwrite(b);
            brelse(b);
        }

        // Step 5: write new parity (if parity disk not failed)
        if ((int)parity_disk != failed_disk)
        {
            struct buf *b = bread(dev, parity_phys);
            memmove(b->data, new_parity, BSIZE);
            bwrite(b);
            brelse(b);
        }
        kfree(old_data);
        kfree(old_parity);
        kfree(new_parity);
    }
    else
    {
        panic("swrite: unsupported RAID mode");
    }
}