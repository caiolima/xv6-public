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

int s5fs_init(void);
int s5_mount(struct inode *, struct inode *);
int s5_unmount(struct inode *);
struct inode* s5_getroot();

struct vfs_operations s5_ops = {
  .fs_init = &s5fs_init,
  .mount = &s5_mount,
  .unmount = &s5_unmount,
  .getroot = &s5_getroot
};

struct filesystem_type s5fs = {
  .name = "s5",
  .ops = &s5_ops
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
  readsb(devi->minor, &sb[devi->minor]);

  // Read the root device
  struct inode *devrtip = iget(devi->minor, ROOTINO);

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
s5_getroot(void)
{
  return 0;
}

