struct inode;

struct vfs_operations {
  int (*mount)(struct inode *, struct inode *);
  int (*unmount)(struct inode *);
  struct inode * (*getroot)();
}

struct filesystem_type {
  char *name;                 // The filesystem name. Its is used by the mount syscall
  int (*fs_init)();           // Filesystem init handler
  struct vfs_operations *ops; // VFS operations
  int fs_type;                // Indes to fast acces the FS on FS Switch Table
}

