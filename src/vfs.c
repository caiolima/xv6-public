/* * Virtual File System implementation
 *  This layer is responsible to implement the
 *  abstraction layer over
 * */

#include "param.h"
#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "list.h"
#include "vfs.h"

/*
 * Its is a pool to allocate vfs structs.
 * We use it becase we don't have a kmalloc function.
 * With an kmalloc implementatios, it need to be removed.
 */
static struct {
  struct spinlock lock;
  struct vfs vfsentry[MAXVFSSIZE];
} vfspool;

struct vfs*
allocvfs()
{
  struct vfs *vfs;

  acquire(&vfspool.lock);
  for (vfs = &vfspool.vfsentry[0]; vfs < &vfspool.vfsentry[MAXVFSSIZE]; vfs++) {
    if (vfs->flag == VFS_FREE) {
      vfs->flag |= VFS_USED;
      release(&vfspool.lock);

      return vfs;
    }
  }
  release(&vfspool.lock);

  return 0;
}

// Add rootvfs on the list
void
installrootfs(void)
{
  if ((rootfs = allocvfs()) == 0) {
    panic("Failed on rootfs allocation");
  }

  rootfs->major = IDEMAJOR;
  rootfs->minor = ROOTDEV;

  struct filesystem_type *fst = getfs(ROOTFSTYPE);
  if (fst == 0) {
    panic("The root fs type is not supported");
  }

  rootfs->fs_t = fst;

  acquire(&vfsmlist.lock);
  list_add_tail(&(rootfs->fs_next), &(vfsmlist.fs_list));
  release(&vfsmlist.lock);
}

void
initvfsmlist(void)
{
  initlock(&vfsmlist.lock, "vfsmlist");
  initlock(&vfspool.lock, "vfspol");
  INIT_LIST_HEAD(&(vfsmlist.fs_list));
}

struct vfs*
getvfsentry(int major, int minor)
{
  struct vfs *vfs;

  list_for_each_entry(vfs, &(vfsmlist.fs_list), fs_next) {
    if (vfs->major == major && vfs->minor == minor) {
      return vfs;
    }
  }

  return 0;
}

int
putvfsonlist(int major, int minor, struct filesystem_type *fs_t)
{
  struct vfs* nvfs;

  if ((nvfs = allocvfs()) == 0) {
    return -1;
  }

  nvfs->major = major;
  nvfs->minor = minor;
  nvfs->fs_t  = fs_t;

  acquire(&vfsmlist.lock);
  list_add_tail(&(nvfs->fs_next), &(vfsmlist.fs_list));
  release(&vfsmlist.lock);

  return 0;
}

struct {
  struct spinlock lock;
  struct list_head fs_list;
} vfssw;

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

