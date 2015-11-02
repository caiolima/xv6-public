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
  struct ext2_sb_info sb[MAXVFSSIZE];
} ext2_sb_pool; // It is a Pool of S5 Superblock Filesystems

/*
 * Its is a pool to allocate ext2 inodes structs.
 * We use it becase we don't have a kmalloc function.
 * With an kmalloc implementatios, it need to be removed.
 */
static struct {
  struct spinlock lock;
  struct ext2_inode ext2_i_entry[NINODE];
} ext2_inode_pool;

struct ext2_sb_info*
alloc_ext2_sb()
{
  struct ext2_sb_info *sb;

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

struct ext2_inode*
alloc_ext2_inode()
{
  struct ext2_inode *ip;

  acquire(&ext2_inode_pool.lock);
  for (ip = &ext2_inode_pool.ext2_i_entry[0]; ip < &ext2_inode_pool.ext2_i_entry[NINODE]; ip++) {
    if (ip->flag == INODE_FREE) {
      ip->flag |= INODE_USED;
      release(&ext2_inode_pool.lock);

      return ip;
    }
  }
  release(&ext2_inode_pool.lock);

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
  return ext2_iget(minor, EXT2_ROOT_INO);
}

static inline int test_root(int a, int b)
{
  int num = b;

  while (a > num)
    num *= b;
  return num == a;
}

static int ext2_group_sparse(int group)
{
  if (group <= 1)
    return 1;
  return (test_root(group, 3) || test_root(group, 5) ||
      test_root(group, 7));
}

/**
 *  ext2_bg_has_super - number of blocks used by the superblock in group
 *  @sb: superblock for filesystem
 *  @group: group number to check
 *
 *  Return the number of blocks used by the superblock (primary or backup)
 *  in this group.  Currently this will be only 0 or 1.
 */
int
ext2_bg_has_super(struct superblock *sb, int group)
{
  if (EXT2_HAS_RO_COMPAT_FEATURE(sb, EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)&&
      !ext2_group_sparse(group))
    return 0;
  return 1;
}

static unsigned long
descriptor_loc(struct superblock *sb,
               unsigned long logic_sb_block,
               int nr)
{
  unsigned long bg, first_meta_bg;
  int has_super = 0;

  first_meta_bg = EXT2_SB(sb)->s_es->s_first_meta_bg;

  if (!EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_META_BG) ||
      nr < first_meta_bg)
    return (logic_sb_block + nr + 1);
  bg = EXT2_SB(sb)->s_desc_per_block * nr;
  if (ext2_bg_has_super(sb, bg))
    has_super = 1;

  return ext2_group_first_block_no(sb, bg) + has_super;
}

void
ext2_readsb(int dev, struct superblock *sb)
{
  struct buf *bp;
  struct ext2_sb_info *sbi;
  struct ext2_superblock *es;
  uint32 blocksize = EXT2_MIN_BLKSIZE;
  int db_count, i;
  unsigned long block;
  unsigned long logic_sb_block = 1;
  /* unsigned long offset = 0; */

  if((sb->flags & SB_NOT_LOADED) == 0) {
    sbi = alloc_ext2_sb(); // Allocate a new S5 sb struct to the superblock.
  } else{
    sbi = sb->fs_info;
  }

  // These sets are needed because of bread
  sb->major = IDEMAJOR;
  sb->minor = dev;
  sb->blocksize = blocksize;
  sb->fs_info = sbi;

  bp = ext2_ops.bread(dev, logic_sb_block); // Read the 1024 bytes starting from the byte 1024
  es = (struct ext2_superblock *)bp->data;

  sbi->s_es = es;
  sbi->s_sbh = bp;
  if (es->s_magic != EXT2_SUPER_MAGIC) {
    ext2_ops.brelse(bp);
    panic("Try to mount a non ext2 fs as an ext2 fs");
  }

  blocksize = EXT2_MIN_BLKSIZE << es->s_log_block_size;
  sb->blocksize = blocksize;

  if (es->s_rev_level == EXT2_GOOD_OLD_REV) {
    sbi->s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    sbi->s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
  } else {
    sbi->s_inode_size = es->s_inode_size;
    sbi->s_first_ino = es->s_first_ino;
  }

  sbi->s_blocks_per_group = es->s_blocks_per_group;
  sbi->s_inodes_per_group = es->s_inodes_per_group;

  sbi->s_inodes_per_block = sb->blocksize / sbi->s_inode_size;
  sbi->s_itb_per_group = sbi->s_inodes_per_group / sbi->s_inodes_per_block;
  sbi->s_desc_per_block = sb->blocksize / sizeof(struct ext2_group_desc);

  if (sbi->s_blocks_per_group > sb->blocksize * 8) {
    panic("error: #blocks per group too big");
  }

  if (sbi->s_inodes_per_group > sb->blocksize * 8) {
    panic("error: #inodes per group too big");
  }

  sbi->s_groups_count = ((es->s_blocks_count -
                          es->s_first_data_block - 1)
                            / sbi->s_blocks_per_group) + 1;
  db_count = (sbi->s_groups_count + sbi->s_desc_per_block - 1) /
              sbi->s_desc_per_block;

  if (db_count > EXT2_MAX_BGC) {
    panic("error: not enough memory to storage s_group_desc. Consider change the EXT2_MAX_BGC constant");
  }

  /* bgl_lock_init(sbi->s_blockgroup_lock); */

  for (i = 0; i < db_count; i++) {
    block = descriptor_loc(sb, logic_sb_block, i);
    sbi->s_group_desc[i] = ext2_ops.bread(dev, block);
    if (!sbi->s_group_desc[i]) {
      panic("Error on read ext2  group descriptor");
    }
  }

  cprintf("Finished the superblock read \n");
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
  struct ext2_inode *ext2ip;

  ext2ip = alloc_ext2_inode();
  if (!ext2ip) {
    panic("No ext2 inode available");
  }

  ip->i_private = ext2ip;

  return 1;
}

struct inode*
ext2_iget(uint dev, uint inum)
{
  return iget(dev, inum, &ext2_fill_inode);
}

