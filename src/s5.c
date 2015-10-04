// It is the s5 filesystem implementation

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "vfs.h"
#include "vfsmount.h"

// Filesystem operations
int            s5fs_init(void);
int            s5_mount(struct inode *, struct inode *);
int            s5_unmount(struct inode *);
struct inode*  s5_getroot();
void           s5_readsb(int dev, struct superblock *sb);
struct inode*  s5_ialloc(uint dev, short type);
uint           s5_balloc(uint dev);
void           s5_bzero(int dev, int bno);
void           s5_bfree(int dev, uint b);

// Inode operations of s5 Filesystem
/* struct inode*  s5_dirlookup(struct inode *dp, char *name, int *off); */
/* void           s5_iupdate(struct inode *ip); */
/* void           s5_itrunc(struct inode *ip); */
/* uint           s5_bmap(struct inode *ip, uint bn); */
/* void           s5_ilock(struct inode* ip); */
/* void           s5_iunlock(struct inode* ip); */
/* void           s5_stati(struct inode *ip, struct stat *st); */
/* int            s5_readi(struct inode *ip, char *dst, uint off, uint n); */
/* int            s5_writei(struct inode *ip, char *src, uint off, uint n); */
/* int            s5_namecmp(const char *s, const char *t); */
/* int            s5_dirlink(struct inode *dp, char *name, uint inum); */

struct inode_operations s5_iops = {
  /* .dirlookup = &s5_dirlookup, */
  /* .iupdate   = &s5_iupdate, */
  /* .itrunc    = &s5_itrunc, */
  /* .bmap      = &s5_bmap, */
  /* .ilock     = &s5_ilock, */
  /* .iunlock   = &s5_iunlock, */
  /* .stati     = &s5_stati, */
  /* .readi     = &s5_readi, */
  /* .writei    = &s5_writei, */
  /* .namecmp   = &s5_namecmp, */
  /* .dirlink   = &s5_dirlink */
};

struct vfs_operations s5_ops = {
  .fs_init = &s5fs_init,
  .mount   = &s5_mount,
  .unmount = &s5_unmount,
  .getroot = &s5_getroot,
  .readsb  = &s5_readsb,
  .ialloc  = &s5_ialloc,
  .balloc  = &s5_balloc,
  .bzero   = &s5_bzero,
  .bfree   = &s5_bfree,
  .brelse  = &brelse,
  .bwrite  = &bwrite,
  .bread   = &bread
};

struct filesystem_type s5fs = {
  .name = "s5",
  .ops = &s5_ops,
  .iops = &s5_iops
};


int
inits5fs(void)
{
  return register_fs(&s5fs);
}

int
s5fs_init(void)
{
  return 0;
}

int
s5_mount(struct inode *devi, struct inode *ip)
{
  struct mntentry *mp;

  // Read the Superblock
  s5_ops.readsb(devi->minor, &sb[devi->minor]);

  // Read the root device
  struct inode *devrtip = s5_ops.getroot(devi->major, devi->minor);

  acquire(&mtable.lock);
  for (mp = &mtable.mpoint[0]; mp < &mtable.mpoint[MOUNTSIZE]; mp++) {
    // This slot is available
    if (mp->flag == 0) {
found_slot:
      mp->dev = devi->minor;
      mp->m_inode = ip;
      mp->pdata = &sb[devi->minor];
      mp->flag |= M_USED;
      mp->m_rtinode = devrtip;

      release(&mtable.lock);

      initlog(devi->minor);
      return 0;
    } else {
      // The disk is already mounted
      if (mp->dev == devi->minor) {
        release(&mtable.lock);
        return -1;
      }

      if (ip->dev == mp->m_inode->dev && ip->inum == mp->m_inode->inum)
        goto found_slot;
    }
  }
  release(&mtable.lock);

  return -1;
}

int
s5_unmount(struct inode *devi)
{
  return 0;
}

struct inode *
s5_getroot(int major, int minor)
{
  return iget(minor, ROOTINO);
}

void
s5_readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = s5_ops.bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  s5_ops.brelse(bp);
}

struct inode*
s5_ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb[dev].ninodes; inum++){
    bp = s5_ops.bread(dev, IBLOCK(inum, sb[dev]));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      s5_ops.brelse(bp);
      return iget(dev, inum);
    }
    s5_ops.brelse(bp);
  }
  panic("ialloc: no inodes");
}

uint
s5_balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for (b = 0; b < sb[dev].size; b += BPB) {
    bp = s5_ops.bread(dev, BBLOCK(b, sb[dev]));
    for (bi = 0; bi < BPB && b + bi < sb[dev].size; bi++) {
      m = 1 << (bi % 8);
      if ((bp->data[bi/8] & m) == 0) {  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        s5_ops.brelse(bp);
        s5_ops.bzero(dev, b + bi);
        return b + bi;
      }
    }
    s5_ops.brelse(bp);
  }
  panic("balloc: out of blocks");
}

void
s5_bzero(int dev, int bno)
{
  struct buf *bp;

  bp = s5_ops.bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  s5_ops.brelse(bp);
}

void
s5_bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  s5_ops.readsb(dev, &sb[dev]);
  bp = s5_ops.bread(dev, BBLOCK(b, sb[dev]));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  s5_ops.brelse(bp);
}

