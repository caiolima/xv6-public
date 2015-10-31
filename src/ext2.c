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
  /* initlock(&ext2_sb_pool.lock, "s5_sb_pool"); */
  /* initlock(&ext2_inode_pool.lock, "s5_inode_pool"); */
  return register_fs(&ext2fs);
}

int
s5_mount(struct inode *devi, struct inode *ip)
{
  panic("ext2 op not defined");

  return -1;
}

int
s5_unmount(struct inode *devi)
{
  panic("ext2 op not defined");
  return 0;
}

struct inode *
s5_getroot(int major, int minor)
{
  panic("ext2 op not defined");
  return 0;
}

void
s5_readsb(int dev, struct superblock *sb)
{
  panic("ext2 op not defined");
}

struct inode*
s5_ialloc(uint dev, short type)
{
  panic("ext2 op not defined");
}

uint
s5_balloc(uint dev)
{
  panic("ext2 op not defined");
}

void
s5_bzero(int dev, int bno)
{
  panic("ext2 op not defined");
}

void
s5_bfree(int dev, uint b)
{
  panic("ext2 op not defined");
}

struct inode*
s5_dirlookup(struct inode *dp, char *name, uint *poff)
{
  panic("ext2 op not defined");

  return 0;
}

void
s5_iupdate(struct inode *ip)
{
  panic("ext2 op not defined");
}

void
s5_itrunc(struct inode *ip)
{
  panic("ext2 op not defined");
}

void
s5_cleanup(struct inode *ip)
{
  panic("ext2 op not defined");
}

uint
s5_bmap(struct inode *ip, uint bn)
{
  panic("ext2 op not defined");
}

void
s5_ilock(struct inode *ip)
{
  panic("ext2 op not defined");
}

int
s5_readi(struct inode *ip, char *dst, uint off, uint n)
{
  panic("ext2 op not defined");
  return 0;
}

int
s5_writei(struct inode *ip, char *src, uint off, uint n)
{
  panic("ext2 op not defined");
  return 0;
}

int
s5_isdirempty(struct inode *dp)
{
  panic("ext2 op not defined");
  return 1;
}

int
s5_unlink(struct inode *dp, uint off)
{
  panic("ext2 op not defined");
  return 0;
}

int
s5_namecmp(const char *s, const char *t)
{
  panic("ext2 op not defined");
  return 0;
}

int
s5_fill_inode(struct inode *ip) {
  panic("ext2 op not defined");

  return 0;
}

struct inode*
s5_iget(uint dev, uint inum)
{
  panic("ext2 op not defined");
  return 0;
}

