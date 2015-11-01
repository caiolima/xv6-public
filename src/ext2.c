#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "vfs.h"
#include "buf.h"
#include "file.h"
#include "vfsmount.h"
#include "ext2.h"

static struct {
  struct spinlock lock;
  struct ext2_superblock sb[MAXVFSSIZE];
} ext2_sb_pool; // It is a Pool of S5 Superblock Filesystems

struct ext2_superblock*
alloc_ext2_sb()
{
  struct ext2_superblock *sb;

  acquire(&ext2_sb_pool.lock);
  for (sb = &ext2_sb_pool.sb[0]; sb < &ext2_sb_pool.sb[MAXVFSSIZE]; sb++) {
    if (sb->flags == SB_FREE) {
      sb->flags |= SB_USED;
      release(&ext2_sb_pool.lock);

      return sb;
    }
  }
  release(&ext2_sb_pool.lock);

  return 0;
}

struct vfs_operations ext2_ops = {
  .fs_init = &ext2fs_init,
  .mount   = &ext2_mount,
  .unmount = &ext2_unmount,
  .getroot = &ext2_getroot,
  .readsb  = &ext2_readsb,
  .ialloc  = &ext2_ialloc,
  .balloc  = &ext2_balloc,
  .bzero   = &ext2_bzero,
  .bfree   = &ext2_bfree,
  .brelse  = &brelse,
  .bwrite  = &bwrite,
  .bread   = &bread,
  .namecmp = &ext2_namecmp
};

struct inode_operations ext2_iops = {
  .dirlookup  = &ext2_dirlookup,
  .iupdate    = &ext2_iupdate,
  .itrunc     = &ext2_itrunc,
  .cleanup    = &ext2_cleanup,
  .bmap       = &ext2_bmap,
  .ilock      = &ext2_ilock,
  .iunlock    = &generic_iunlock,
  .stati      = &generic_stati,
  .readi      = &ext2_readi,
  .writei     = &ext2_writei,
  .dirlink    = &generic_dirlink,
  .unlink     = &ext2_unlink,
  .isdirempty = &ext2_isdirempty
};

struct filesystem_type ext2fs = {
  .name = "ext2",
  .ops = &ext2_ops,
  .iops = &ext2_iops
};

int
initext2fs(void)
{
  initlock(&ext2_sb_pool.lock, "ext2_sb_pool");
  /* initlock(&ext2_inode_pool.lock, "ext2_inode_pool"); */
  return register_fs(&ext2fs);
}

int
ext2fs_init(void)
{
  return 0;
}

int
ext2_mount(struct inode *devi, struct inode *ip)
{
  /* struct mntentry *mp; */

  // Read the Superblock
  ext2_ops.readsb(devi->minor, &sb[devi->minor]);

  // Read the root device
  ext2_ops.getroot(devi->major, devi->minor);

  return -1;
}

int
ext2_unmount(struct inode *devi)
{
  panic("ext2 op not defined");
  return 0;
}

struct inode *
ext2_getroot(int major, int minor)
{
  panic("ext2 op not defined");
  return 0;
}

void
ext2_readsb(int dev, struct superblock *sb)
{
  struct buf *bp;
  struct ext2_superblock *ext2sb;
  uint32 blocksize;

  if((sb->flags & SB_NOT_LOADED) == 0) {
    ext2sb = alloc_ext2_sb(); // Allocate a new S5 sb struct to the superblock.
  } else{
    ext2sb = sb->fs_info;
  }

  // These sets are needed because of bread
  sb->major = IDEMAJOR;
  sb->minor = dev;
  sb->blocksize = EXT2_MIN_BLKSIZE;

  bp = ext2_ops.bread(dev, 1); // Read the 1024 bytes starting from the byte 1024
  memmove(ext2sb, bp->data, sizeof(*ext2sb) - sizeof(ext2sb->flags));
  ext2_ops.brelse(bp);

  if (ext2sb->s_magic != EXT2_SUPER_MAGIC) {
    panic("Try to mount a non ext2 fs as an ext2 fs");
  }

  cprintf("Block size is: %d", ext2sb->s_log_block_size);
  sb->fs_info = ext2sb;
}

struct inode*
ext2_ialloc(uint dev, short type)
{
  panic("ext2 op not defined");
}

uint
ext2_balloc(uint dev)
{
  panic("ext2 op not defined");
}

void
ext2_bzero(int dev, int bno)
{
  panic("ext2 op not defined");
}

void
ext2_bfree(int dev, uint b)
{
  panic("ext2 op not defined");
}

struct inode*
ext2_dirlookup(struct inode *dp, char *name, uint *poff)
{
  panic("ext2 op not defined");

  return 0;
}

void
ext2_iupdate(struct inode *ip)
{
  panic("ext2 op not defined");
}

void
ext2_itrunc(struct inode *ip)
{
  panic("ext2 op not defined");
}

void
ext2_cleanup(struct inode *ip)
{
  panic("ext2 op not defined");
}

uint
ext2_bmap(struct inode *ip, uint bn)
{
  panic("ext2 op not defined");
}

void
ext2_ilock(struct inode *ip)
{
  panic("ext2 op not defined");
}

int
ext2_readi(struct inode *ip, char *dst, uint off, uint n)
{
  panic("ext2 op not defined");
  return 0;
}

int
ext2_writei(struct inode *ip, char *src, uint off, uint n)
{
  panic("ext2 op not defined");
  return 0;
}

int
ext2_isdirempty(struct inode *dp)
{
  panic("ext2 op not defined");
  return 1;
}

int
ext2_unlink(struct inode *dp, uint off)
{
  panic("ext2 op not defined");
  return 0;
}

int
ext2_namecmp(const char *s, const char *t)
{
  panic("ext2 op not defined");
  return 0;
}

int
ext2_fill_inode(struct inode *ip) {
  panic("ext2 op not defined");

  return 0;
}

struct inode*
ext2_iget(uint dev, uint inum)
{
  panic("ext2 op not defined");
  return 0;
}

