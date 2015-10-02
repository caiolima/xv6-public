// Simple VFS implementation

#include "list.h"

#ifndef XV6_VFS_H_
#define XV6_VFS_H_

struct inode;

struct vfs_operations {
  int (*fs_init)(void);
  int (*mount)(struct inode *, struct inode *);
  int (*unmount)(struct inode *);
  struct inode *(*getroot)(void);
};

struct filesystem_type {
  char *name;                 // The filesystem name. Its is used by the mount syscall
  struct vfs_operations *ops; // VFS operations
  struct list_head fs_list;  // This is a list of Filesystems used by vfssw
};

#endif /* XV6_VFS_H_ */

