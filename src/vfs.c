/* * Virtual File System implementation
 *  This layer is respnsible to implement the
 *  abstraction layer over
 * */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "list.h"
#include "vfs.h"

struct {
  struct spinlock lock;
  struct list_head fs_list;
} vfssw;

void
initvfs(void)
{
  initlock(&vfs.lock, "vfs");
}

void
initvfssw(void)
{
  initlock(&vfssw.lock, "vfssw");
  INIT_LIST_HEAD(&(vfssw.fs_list));
}

int
register_fs(struct filesystem_type *fs)
{
  acquire(&vfssw.lock);
  list_add(&(fs->fs_list), &(vfssw.fs_list));
  release(&vfssw.lock);

  return 0;
}

struct filesystem_type*
getfs(const char *fs_name)
{
  struct filesystem_type *fs;

  list_for_each_entry(fs, &(vfssw.fs_list), fs_list) {
    if (strcmp(fs_name, fs->name) == 0) {
      return fs;
    }
  }

  return 0;
}

