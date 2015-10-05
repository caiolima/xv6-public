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
int            s5_namecmp(const char *s, const char *t);

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
  .bread   = &bread,
  .namecmp   = &s5_namecmp
};

// Inode operations of s5 Filesystem
struct inode*  s5_dirlookup(struct inode *dp, char *name, uint *off);
void           s5_iupdate(struct inode *ip);
void           s5_itrunc(struct inode *ip);
uint           s5_bmap(struct inode *ip, uint bn);
void           s5_ilock(struct inode* ip);
void           s5_iunlock(struct inode* ip);
void           s5_stati(struct inode *ip, struct stat *st);
int            s5_readi(struct inode *ip, char *dst, uint off, uint n);
int            s5_writei(struct inode *ip, char *src, uint off, uint n);
int            s5_dirlink(struct inode *dp, char *name, uint inum);

struct inode_operations s5_iops = {
  .dirlookup = &s5_dirlookup,
  .iupdate   = &s5_iupdate,
  .itrunc    = &s5_itrunc,
  .bmap      = &s5_bmap,
  .ilock     = &s5_ilock,
  .iunlock   = &generic_iunlock,
  .stati     = &generic_stati,
  .readi     = &generic_readi,
  .writei    = &s5_writei,
  .dirlink   = &generic_dirlink
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

struct inode*
s5_dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type == T_FILE || dp->type == T_DEV)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(s5_iops.readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      continue;
    if(s5_ops.namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

void
s5_iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = s5_ops.bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  s5_ops.brelse(bp);
}

void
s5_itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      s5_ops.bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = s5_ops.bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        s5_ops.bfree(ip->dev, a[j]);
    }
    s5_ops.brelse(bp);
    s5_ops.bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  s5_iops.iupdate(ip);
}

uint
s5_bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = s5_ops.balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = s5_ops.balloc(ip->dev);
    bp = s5_ops.bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = s5_ops.balloc(ip->dev);
      log_write(bp);
    }
    s5_ops.brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

void
s5_ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while (ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if (!(ip->flags & I_VALID)) {
    bp = s5_ops.bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    s5_ops.brelse(bp);
    ip->flags |= I_VALID;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

int
s5_writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = s5_ops.bread(ip->dev, s5_iops.bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    s5_ops.brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    s5_iops.iupdate(ip);
  }
  return n;
}

