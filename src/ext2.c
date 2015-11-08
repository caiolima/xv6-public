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

typedef struct {
  uint32 *p;
  uint32 key;
  struct buf *bh;
} Indirect;

static inline void
add_chain(Indirect *p, struct buf *bh, uint32 *v)
{
  p->key = *(p->p = v);
  p->bh = bh;
}

static inline int verify_chain(Indirect *from, Indirect *to)
{
  while (from <= to && from->key == *from->p)
    from++;
  return (from > to);
}


static struct {
  struct spinlock lock;
  struct ext2_inode_info ei[NINODE];
} ext2_ei_pool; // It is a Pool of S5 Superblock Filesystems

struct ext2_inode_info*
alloc_ext2_inode_info()
{
  struct ext2_inode_info *ei;

  acquire(&ext2_ei_pool.lock);
  for (ei = &ext2_ei_pool.ei[0]; ei < &ext2_ei_pool.ei[NINODE]; ei++) {
    if (ei->flags == INODE_FREE) {
      ei->flags |= INODE_USED;
      release(&ext2_ei_pool.lock);

      return ei;
    }
  }
  release(&ext2_ei_pool.lock);

  return 0;
}

static struct {
  struct spinlock lock;
  struct ext2_sb_info sb[MAXVFSSIZE];
} ext2_sb_pool; // It is a Pool of S5 Superblock Filesystems

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
  .readi      = &generic_readi,
  .writei     = &ext2_writei,
  .dirlink    = &ext2_dirlink,
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
  struct mntentry *mp;

  // Read the Superblock
  ext2_ops.readsb(devi->minor, &sb[devi->minor]);

  // Read the root device
  struct inode *devrtip = ext2_ops.getroot(devi->major, devi->minor);

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
ext2_unmount(struct inode *devi)
{
  panic("ext2 unmount op not defined");
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

struct ext2_group_desc *
ext2_get_group_desc(struct superblock * sb,
                    unsigned int block_group,
                    struct buf ** bh)
{
  unsigned long group_desc;
  unsigned long offset;
  struct ext2_group_desc * desc;
  struct ext2_sb_info *sbi = EXT2_SB(sb);

  if (block_group >= sbi->s_groups_count) {
    panic("Block group # is too large");
  }

  group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(sb);
  offset = block_group & (EXT2_DESC_PER_BLOCK(sb) - 1);
  if (!sbi->s_group_desc[group_desc]) {
    panic("Accessing a group descriptor not loaded");
  }

  desc = (struct ext2_group_desc *) sbi->s_group_desc[group_desc]->data;
  if (bh)
    *bh = sbi->s_group_desc[group_desc];
  return desc + offset;
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
  unsigned long offset = 0;

  if((sb->flags & SB_NOT_LOADED) == 0) {
    sbi = alloc_ext2_sb(); // Allocate a new S5 sb struct to the superblock.
  } else{
    sbi = sb->fs_info;
  }

  // These sets are needed because of bread
  sb->major = IDEMAJOR;
  sb->minor = dev;
  sb_set_blocksize(sb, blocksize);
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

  /* If the blocksize doesn't match, re-read the thing.. */
  if (sb->blocksize != blocksize) {
    ext2_ops.brelse(bp);

    sb_set_blocksize(sb, blocksize);

    logic_sb_block = EXT2_MIN_BLKSIZE / blocksize;
    offset = EXT2_MIN_BLKSIZE % blocksize;
    bp = ext2_ops.bread(dev, logic_sb_block);

    if (!bp) {
      panic("Error on second ext2 superblock read");
    }

    es = (struct ext2_superblock *) (((char *)bp->data) + offset);
    sbi->s_es = es;

    if (es->s_magic != EXT2_SUPER_MAGIC) {
      panic("error: ext2 magic mismatch");
    }
  }

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

  sbi->s_addr_per_block_bits = ilog2(EXT2_ADDR_PER_BLOCK(sb));
  sbi->s_desc_per_block_bits = ilog2(EXT2_DESC_PER_BLOCK(sb));

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

  sbi->s_gdb_count = db_count;

}

struct inode*
ext2_ialloc(uint dev, short type)
{
  panic("ext2 ialloc op not defined");
}

uint
ext2_balloc(uint dev)
{
  panic("ext2 balloc op not defined");
}

void
ext2_bzero(int dev, int bno)
{
  panic("ext2 bzero op not defined");
}

void
ext2_bfree(int dev, uint b)
{
  panic("ext2 bfree op not defined");
}

struct inode*
ext2_dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum, currblk;
  struct ext2_dir_entry_2 *de;
  struct buf *bh;
  int namelen = strlen(name);

  for (off = 0; off < dp->size;) {
    currblk = off / sb[dp->dev].blocksize;

    bh = ext2_ops.bread(dp->dev, ext2_iops.bmap(dp, currblk));

    de = (struct ext2_dir_entry_2 *) (bh->data + (off % sb[dp->dev].blocksize));

    if(de->inode == 0 || de->name_len != namelen) {
      off += de->rec_len;
      ext2_ops.brelse(bh);
      continue;
    }

    if(strncmp(name, de->name, de->name_len) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de->inode;
      ext2_ops.brelse(bh);
      return ext2_iget(dp->dev, inum);
    }
    off += de->rec_len;
    ext2_ops.brelse(bh);
  }

  return 0;
}

void
ext2_iupdate(struct inode *ip)
{
  panic("ext2 iupdate op not defined");
}

void
ext2_itrunc(struct inode *ip)
{
  panic("ext2 itrunc op not defined");
}

void
ext2_cleanup(struct inode *ip)
{
  memset(ip->i_private, 0, sizeof(struct ext2_inode_info));
}

/**
 * ext2_block_to_path - parse the block number into array of offsets
 * @inode: inode in question (we are only interested in its superblock)
 * @i_block: block number to be parsed
 * @offsets: array to store the offsets in
 * @boundary: set this non-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 * To store the locations of file's data ext2 uses a data structure common
 * for UNIX filesystems - tree of pointers anchored in the inode, with
 * data blocks at leaves and indirect blocks in intermediate nodes.
 * This function translates the block number into path in that tree -
 * return value is the path length and @offsets[n] is the offset of
 * pointer to (n+1)th node in the nth one. If @block is out of range
 * (negative or too large) warning is printed and zero returned.
 *
 * Note: function doesn't find node addresses, so no IO is needed. All
 * we need to know is the capacity of indirect blocks (taken from the
 * superblock).
 */

/*
 * Portability note: the last comparison (check that we fit into triple
 * indirect block) is spelled differently, because otherwise on an
 * architecture with 32-bit longs and 8Kb pages we might get into trouble
 * if our filesystem had 8Kb blocks. We might use long long, but that would
 * kill us on x86. Oh, well, at least the sign propagation does not matter -
 * i_block would have to be negative in the very beginning, so we would not
 * get there at all.
 */

static int
ext2_block_to_path(struct inode *inode,
                   long i_block, int offsets[4])
{
  int ptrs = EXT2_ADDR_PER_BLOCK(&sb[inode->dev]);
  int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(&sb[inode->dev]);
  const long direct_blocks = EXT2_NDIR_BLOCKS,
        indirect_blocks = ptrs,
        double_blocks = (1 << (ptrs_bits * 2));
  int n = 0;

  if (i_block < 0) {
    panic("block_to_path invalid block num");
  } else if (i_block < direct_blocks) {
    offsets[n++] = i_block;
  } else if ((i_block -= direct_blocks) < indirect_blocks) {
    offsets[n++] = EXT2_IND_BLOCK;
    offsets[n++] = i_block;
  } else if ((i_block -= indirect_blocks) < double_blocks) {
    offsets[n++] = EXT2_DIND_BLOCK;
    offsets[n++] = i_block >> ptrs_bits;
    offsets[n++] = i_block & (ptrs - 1);
  } else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
    offsets[n++] = EXT2_TIND_BLOCK;
    offsets[n++] = i_block >> (ptrs_bits * 2);
    offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
    offsets[n++] = i_block & (ptrs - 1);
  } else {
    panic("This block is out of bounds from this ext2 fs");
  }

  return n;
}

/**
 * ext2_get_branch - read the chain of indirect blocks leading to data
 * @inode: inode in question
 * @depth: depth of the chain (1 - direct pointer, etc.)
 * @offsets: offsets of pointers in inode/indirect blocks
 * @chain: place to store the result
 * @err: here we store the error value
 *
 * Function fills the array of triples <key, p, bh> and returns %NULL
 * if everything went OK or the pointer to the last filled triple
 * (incomplete one) otherwise. Upon the return chain[i].key contains
 * the number of (i+1)-th block in the chain (as it is stored in memory,
 * i.e. little-endian 32-bit), chain[i].p contains the address of that
 * number (it points into struct inode for i==0 and into the bh->b_data
 * for i>0) and chain[i].bh points to the buffer_head of i-th indirect
 * block for i>0 and NULL for i==0. In other words, it holds the block
 * numbers of the chain, addresses they were taken from (and where we can
 * verify that chain did not change) and buffer_heads hosting these
 * numbers.
 *
 * Function stops when it stumbles upon zero pointer (absent block)
 *  (pointer to last triple returned, *@err == 0)
 * or when it gets an IO error reading an indirect block
 *  (ditto, *@err == -EIO)
 * or when it notices that chain had been changed while it was reading
 *  (ditto, *@err == -EAGAIN)
 * or when it reads all @depth-1 indirect blocks successfully and finds
 * the whole chain, all way to the data (returns %NULL, *err == 0).
 */
static Indirect *ext2_get_branch(struct inode *inode,
                                 int depth,
                                 int *offsets,
                                 Indirect chain[4])
{
  Indirect *p = chain;
  struct buf *bh;
  struct ext2_inode_info *ei = inode->i_private;

  add_chain (chain, 0, ei->i_ei.i_block + *offsets);
  if (!p->key)
    goto no_block;
  while (--depth) {
    bh = ext2_ops.bread(inode->dev, p->key);
    if (!bh)
      panic("error on ext2_get_branch");
    if (!verify_chain(chain, p))
      panic("ext2_get_branch chain changed");
    add_chain(++p, bh, (uint32*)bh->data + *++offsets);
    if (!p->key)
      goto no_block;
  }
  return 0;

no_block:
  return p;
}

uint
ext2_bmap(struct inode *ip, uint bn)
{
  /* struct buf *bp; */
  int depth;
  Indirect chain[4];
  Indirect *partial;
  int offsets[4];
  uint blkn;

  depth = ext2_block_to_path(ip,bn,offsets);

  if (depth == 0)
    panic("Wrong depth value");

  partial = ext2_get_branch(ip, depth, offsets, chain);

  if (!partial) {
    goto got_it;
  }

got_it:
  blkn = chain[depth-1].key;
  /* Clean up and exit */
  partial = chain + depth - 1;  /* the whole chain */
/* cleanup: */
  while (partial > chain) {
    brelse(partial->bh);
    partial--;
  }

  return blkn;
}

void
ext2_ilock(struct inode *ip)
{
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while (ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  // TODO Here
}

int
ext2_writei(struct inode *ip, char *src, uint off, uint n)
{
  panic("ext2 writei op not defined");
  return 0;
}

int
ext2_dirlink(struct inode *dp, char *name, uint inum)
{
  panic("ext2 dirlink op not defined");
  return 0;
}

int
ext2_isdirempty(struct inode *dp)
{
  panic("ext2 isdirempty op not defined");
  return 1;
}

int
ext2_unlink(struct inode *dp, uint off)
{
  panic("ext2 unlink op not defined");
  return 0;
}

int
ext2_namecmp(const char *s, const char *t)
{
  return strncmp(s, t, EXT2_NAME_LEN);
}

static struct ext2_inode_info *
ext2_get_inode(struct superblock *sb, uint ino)
{
  struct buf * bp;
  unsigned long block_group;
  unsigned long block;
  unsigned long offset;
  struct ext2_group_desc *gdp;
  struct ext2_inode_info *ei;

  ei = alloc_ext2_inode_info();

  if (ei == 0)
    panic("No memory to alloc ext2_inode");

  if ((ino != EXT2_ROOT_INO && ino < EXT2_FIRST_INO(sb)) ||
       ino > EXT2_SB(sb)->s_es->s_inodes_count)
    panic("Ext2 invalid inode number");

  block_group = (ino - 1) / EXT2_INODES_PER_GROUP(sb);
  gdp = ext2_get_group_desc(sb, block_group, 0);
  if (!gdp)
    panic("Invalid group descriptor at ext2_get_inode");

  /*
   * Figure out the offset within the block group inode table
   */
  offset = ((ino - 1) % EXT2_INODES_PER_GROUP(sb)) * EXT2_INODE_SIZE(sb);
  block = gdp->bg_inode_table +
    (offset >> EXT2_BLOCK_SIZE_BITS(sb));

  if (!(bp = ext2_ops.bread(sb->minor, block)))
    panic("Error on read the  block inode");

  offset &= (EXT2_BLOCK_SIZE(sb) - 1);
  memmove(&ei->i_ei, bp->data + offset, sizeof(ei->i_ei));
  ext2_ops.brelse(bp);

  return ei;
}

/**
 * Its is called because the icache lookup failed
 */
int
ext2_fill_inode(struct inode *ip) {
  struct ext2_inode_info *ei;

  ei = ext2_get_inode(&sb[ip->dev], ip->inum);

  ip->i_private = ei;

  // Translate the inode type to xv6 type
  if (S_ISDIR(ei->i_ei.i_mode)) {
    ip->type = T_DIR;
  } else if (S_ISREG(ei->i_ei.i_mode)) {
    ip->type = T_FILE;
  } else if (S_ISCHR(ei->i_ei.i_mode) || S_ISBLK(ei->i_ei.i_mode)) {
    ip->type = T_DEV;
  } else {
    panic("ext2: invalid file mode");
  }

  ip->nlink = ei->i_ei.i_links_count;
  ip->size  = ei->i_ei.i_size;
  return 1;
}

struct inode*
ext2_iget(uint dev, uint inum)
{
  return iget(dev, inum, &ext2_fill_inode);
}

