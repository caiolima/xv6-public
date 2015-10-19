// Simple device driver interface code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "vfs.h"
#include "file.h"
#include "device.h"

struct {
  struct spinlock lock;
  struct bdev_ops *bdeventry[MAXBDEV];
} bdevtable;

void
bdevtableinit(void)
{
  initlock(&bdevtable.lock, "bdevtable");
}

int
registerbdev(struct bdev dev)
{

  if (dev.major > MAXBDEV - 1)
    return -1;

  acquire(&bdevtable.lock);
  bdevtable.bdeventry[dev.major] = dev.ops;
  release(&bdevtable.lock);

  return 0;
}

int
unregisterbdev(struct bdev dev)
{

  if (dev.major > MAXBDEV - 1)
    return -1;

  acquire(&bdevtable.lock);
  bdevtable.bdeventry[dev.major] = 0;
  release(&bdevtable.lock);

  return 0;
}

int
bdev_open(struct inode *devi)
{
  if (devi->major > MAXBDEV - 1)
    return -1;

  struct bdev_ops *bops = bdevtable.bdeventry[devi->major];
  if (bops == 0) return -1;

  return bops->open(devi->minor);
}

