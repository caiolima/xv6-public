/* * Virtual File System implementation
 *  This layer is responsible to implement the
 *  abstraction layer over
 * */

#include "param.h"
#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "list.h"
#include "stat.h"
#include "file.h"
#include "buf.h"
#include "vfs.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

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

void
generic_iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

void
generic_stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

int
generic_readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = ip->fs_t->ops->bread(ip->dev, ip->iops->bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    ip->fs_t->ops->brelse(bp);
  }
  return n;
}

int
generic_dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dp->iops->dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(dp->iops->readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(dp->iops->writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}
