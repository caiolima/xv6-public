/* * Virtual File System implementation
 *  This layer is respnsible to implement the
 *  abstraction layer over
 * */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "list.h"
#include "vfs.h"

struct {
  struct spinlock lock;
  struct filesystem_type *fs;
} vfssw;

void
initvfssw(void)
{
  initlock(&vfssw.lock, "vfssw");
}

int
register_fs(struct filesystem_type *fs)
{
  acquire(&vfssw.lock);
  // If it is the first registered filesystem
  if (vfssw.fs == 0) {
    INIT_LIST_HEAD(&(fs->fs_list));
  }
  release(&vfssw.lock);

  return 0;
}

