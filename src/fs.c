// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation 
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

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

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->flags.
//
// An inode and its in-memory represtative go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, iput() frees if
//   the link count has fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() to find or
//   create a cache entry and increment its ref, iput()
//   to decrement ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when the I_VALID bit
//   is set in ip->flags. ilock() reads the inode from
//   the disk and sets I_VALID, while iput() clears
//   I_VALID if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode. The I_BUSY flag indicates
//   that the inode is locked. ilock() sets I_BUSY,
//   while iunlock clears it.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.

void
iinit(int dev)
{
  initlock(&icache.lock, "icache");
  rootfs->fs_t->ops->readsb(dev, &sb[dev]);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d inodestart %d bmap start %d\n", sb[dev].size,
          sb[dev].nblocks, sb[dev].ninodes, sb[dev].nlog, sb[dev].logstart, sb[dev].inodestart, sb[dev].bmapstart);
}

//PAGEBREAK!
// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb[dev].ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb[dev]));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){

      // If the current inode is an mount point
      if (ip->type == T_MOUNT) {
        struct inode *rinode = mtablertinode(ip);

        if (rinode == 0) {
          panic("Invalid Inode on Mount Table");
        }

        rinode->ref++;

        release(&icache.lock);
        return rinode;
      }

      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->flags = 0;

  struct filesystem_type *fs_t = getvfsentry(IDEMAJOR, dev)->fs_t;

  ip->fs_t = fs_t;
  ip->iops = fs_t->iops;

  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while(ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if(!(ip->flags & I_VALID)){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->flags |= I_VALID;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.
    if(ip->flags & I_BUSY)
      panic("iput busy");
    ip->flags |= I_BUSY;
    release(&icache.lock);
    ip->iops->itrunc(ip);
    ip->type = 0;
    ip->iops->iupdate(ip);
    acquire(&icache.lock);
    ip->flags = 0;
    wakeup(ip);
  }
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  ip->iops->iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
/* static uint */
/* bmap(struct inode *ip, uint bn) */
/* { */
/*   uint addr, *a; */
/*   struct buf *bp; */

/*   if(bn < NDIRECT){ */
/*     if((addr = ip->addrs[bn]) == 0) */
/*       ip->addrs[bn] = addr = ip->fs_t->ops->balloc(ip->dev); */
/*     return addr; */
/*   } */
/*   bn -= NDIRECT; */

/*   if(bn < NINDIRECT){ */
/*     // Load indirect block, allocating if necessary. */
/*     if((addr = ip->addrs[NDIRECT]) == 0) */
/*       ip->addrs[NDIRECT] = addr = ip->fs_t->ops->balloc(ip->dev); */
/*     bp = bread(ip->dev, addr); */
/*     a = (uint*)bp->data; */
/*     if((addr = a[bn]) == 0){ */
/*       a[bn] = addr = ip->fs_t->ops->balloc(ip->dev); */
/*       log_write(bp); */
/*     } */
/*     brelse(bp); */
/*     return addr; */
/*   } */

/*   panic("bmap: out of range"); */
/* } */

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
/* static void */
/* itrunc(struct inode *ip) */
/* { */
/*   int i, j; */
/*   struct buf *bp; */
/*   uint *a; */

/*   for(i = 0; i < NDIRECT; i++){ */
/*     if(ip->addrs[i]){ */
/*       ip->fs_t->ops->bfree(ip->dev, ip->addrs[i]); */
/*       ip->addrs[i] = 0; */
/*     } */
/*   } */

/*   if(ip->addrs[NDIRECT]){ */
/*     bp = bread(ip->dev, ip->addrs[NDIRECT]); */
/*     a = (uint*)bp->data; */
/*     for(j = 0; j < NINDIRECT; j++){ */
/*       if(a[j]) */
/*         ip->fs_t->ops->bfree(ip->dev, a[j]); */
/*     } */
/*     brelse(bp); */
/*     ip->fs_t->ops->bfree(ip->dev, ip->addrs[NDIRECT]); */
/*     ip->addrs[NDIRECT] = 0; */
/*   } */

/*   ip->size = 0; */
/*   iupdate(ip); */
/* } */

// copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
/* int */
/* readi(struct inode *ip, char *dst, uint off, uint n) */
/* { */
/*   uint tot, m; */
/*   struct buf *bp; */

/*   if(ip->type == T_DEV){ */
/*     if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read) */
/*       return -1; */
/*     return devsw[ip->major].read(ip, dst, n); */
/*   } */

/*   if(off > ip->size || off + n < off) */
/*     return -1; */
/*   if(off + n > ip->size) */
/*     n = ip->size - off; */

/*   for(tot=0; tot<n; tot+=m, off+=m, dst+=m){ */
/*     bp = bread(ip->dev, bmap(ip, off/BSIZE)); */
/*     m = min(n - tot, BSIZE - off%BSIZE); */
/*     memmove(dst, bp->data + off%BSIZE, m); */
/*     brelse(bp); */
/*   } */
/*   return n; */
/* } */

// PAGEBREAK!
// Write data to inode.
/* int */
/* writei(struct inode *ip, char *src, uint off, uint n) */
/* { */
/*   uint tot, m; */
/*   struct buf *bp; */

/*   if(ip->type == T_DEV){ */
/*     if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write) */
/*       return -1; */
/*     return devsw[ip->major].write(ip, src, n); */
/*   } */

/*   if(off > ip->size || off + n < off) */
/*     return -1; */
/*   if(off + n > MAXFILE*BSIZE) */
/*     return -1; */

/*   for(tot=0; tot<n; tot+=m, off+=m, src+=m){ */
/*     bp = bread(ip->dev, bmap(ip, off/BSIZE)); */
/*     m = min(n - tot, BSIZE - off%BSIZE); */
/*     memmove(bp->data + off%BSIZE, src, m); */
/*     log_write(bp); */
/*     brelse(bp); */
/*   } */

/*   if(n > 0 && off > ip->size){ */
/*     ip->size = off; */
/*     iupdate(ip); */
/*   } */
/*   return n; */
/* } */

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
/* struct inode* */
/* dirlookup(struct inode *dp, char *name, uint *poff) */
/* { */
/*   uint off, inum; */
/*   struct dirent de; */

/*   if(dp->type == T_FILE || dp->type == T_DEV) */
/*     panic("dirlookup not DIR"); */

/*   for(off = 0; off < dp->size; off += sizeof(de)){ */
/*     if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) */
/*       panic("dirlink read"); */
/*     if(de.inum == 0) */
/*       continue; */
/*     if(namecmp(name, de.name) == 0){ */
/*       // entry matches path element */
/*       if(poff) */
/*         *poff = off; */
/*       inum = de.inum; */
/*       return iget(dp->dev, inum); */
/*     } */
/*   } */

/*   return 0; */
/* } */

// Write a new directory entry (name, inum) into the directory dp.
/* int */
/* dirlink(struct inode *dp, char *name, uint inum) */
/* { */
/*   int off; */
/*   struct dirent de; */
/*   struct inode *ip; */

/*   // Check that name is not present. */
/*   if((ip = dirlookup(dp, name, 0)) != 0){ */
/*     iput(ip); */
/*     return -1; */
/*   } */

/*   // Look for an empty dirent. */
/*   for(off = 0; off < dp->size; off += sizeof(de)){ */
/*     if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) */
/*       panic("dirlink read"); */
/*     if(de.inum == 0) */
/*       break; */
/*   } */

/*   strncpy(de.name, name, DIRSIZ); */
/*   de.inum = inum; */
/*   if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) */
/*     panic("dirlink"); */

/*   return 0; */
/* } */

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = rootfs->fs_t->ops->getroot(IDEMAJOR, ROOTDEV);
  else
    ip = idup(proc->cwd);

  while((path = skipelem(path, name)) != 0){
    ip->iops->ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      ip->iops->iunlock(ip);
      return ip;
    }

    component_search:
    if((next = ip->iops->dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }

    if (next->inum == ROOTINO && isinoderoot(ip) && (strncmp(name, "..", 2) == 0)) {
      struct inode *mntinode = mtablemntinode(ip);
      iunlockput(ip);
      ip = mntinode;
      ip->iops->ilock(ip);
      ip->ref++;
      goto component_search;
    }

    iunlockput(ip);

    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
