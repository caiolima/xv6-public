// Simple VFS implementation

#include "types.h"
#include "param.h"
#include "list.h"
#include "fs.h"

#ifndef XV6_VFS_H_
#define XV6_VFS_H_

struct inode_operations {
  struct inode* (*dirlookup)(struct inode *dp, char *name, int *off);
  void (*iupdate)(struct inode *ip);
  void (*itrunc)(struct inode *ip);
  uint (*bmap)(struct inode *ip, uint bn);
  void (*ilock)(struct inode* ip);
  void (*iunlock)(struct inode* ip);
  void (*stati)(struct inode *ip, struct stat *st);
  int (*readi)(struct inode *ip, char *dst, uint off, uint n);
  int (*writei)(struct inode *ip, char *src, uint off, uint n);
  int (*namecmp)(const char *s, const char *t);
  int (*dirlink)(struct inode *dp, char *name, uint inum);
};

// in-memory copy of an inode
struct inode {
  uint dev;                     // Device number
  uint inum;                    // Inode number
  int ref;                      // Reference count
  int flags;                    // I_BUSY, I_VALID
  struct filesystem_type *fs_t; // The Filesystem type this inode is stored in
  struct inode_operatins *iops; // The specific inode operations

  short type;                   // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};
#define I_BUSY 0x1
#define I_VALID 0x2

struct vfs_operations {
  int (*fs_init)(void);
  int (*mount)(struct inode *, struct inode *);
  int (*unmount)(struct inode *);
  struct inode *(*getroot)(int, int);
  int (*ialloc)(int dev, int type);
  struct buf* (*balloc)(int dev);
  void (*bfree)(int dev, uint b);
  void (*brelse)(struct buf *b);
  void (*bwrite)(struct buf *b);
  struct buf* (*bread)(uint dev, uint blockno);
};

/*
 * This is struct is the map block device and its filesystem.
 * It is supporting only one major block device.
 */
struct {
  struct spinlock lock;
  struct filesystem_type *fstype[MAXBDEV];
} vfs;

struct filesystem_type {
  char *name;                 // The filesystem name. Its is used by the mount syscall
  struct vfs_operations *ops; // VFS operations
  struct list_head fs_list;   // This is a list of Filesystems used by vfssw
};

void            initvfs(void);
void            initvfssw(void);
int             register_fs(struct filesystem_type *fs);
struct filesystem_type* getfs(const char *fs_name);

#endif /* XV6_VFS_H_ */

