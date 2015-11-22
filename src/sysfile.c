//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"
#include "vfs.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=proc->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;

  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd] == 0){
      proc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;
  
  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;
  
  if(argfd(0, &fd, &f) < 0)
    return -1;
  proc->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ip->iops->ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  ip->iops->iupdate(ip);
  ip->iops->iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  dp->iops->ilock(dp);
  if(dp->dev != ip->dev || dp->iops->dirlink(dp, name, ip->inum, ip->type) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ip->iops->ilock(ip);
  ip->nlink--;
  ip->iops->iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  dp->iops->ilock(dp);

  // Cannot unlink "." or "..".
  if(dp->fs_t->ops->namecmp(name, ".") == 0 || dp->fs_t->ops->namecmp(name, "..") == 0)
    goto bad;

  if((ip = dp->iops->dirlookup(dp, name, &off)) == 0)
    goto bad;
  ip->iops->ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !ip->iops->isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  if(dp->iops->unlink(dp, off) == -1)
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    dp->iops->iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  ip->iops->iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  uint off;
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  dp->iops->ilock(dp);

  if((ip = dp->iops->dirlookup(dp, name, &off)) != 0){
    iunlockput(dp);
    ip->iops->ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = dp->fs_t->ops->ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ip->iops->ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->iops->iupdate(ip);

  if (type == T_DIR) {  // Create . and .. entries.
    dp->nlink++;  // for ".."
    dp->iops->iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (ip->iops->dirlink(ip, ".", ip->inum, ip->type) < 0 || ip->iops->dirlink(ip, "..", dp->inum, dp->type) < 0)
      panic("create dots");
  }

  cprintf("Created dots\n");

  if(dp->iops->dirlink(dp, name, ip->inum, ip->type) < 0)
    panic("create: dirlink");

  cprintf("Linked with parent\n");

  iunlockput(dp);

  cprintf("Finished iunlockput\n");

  return ip;
}

int
sys_mount(void)
{
  char *devf;
  char *path;
  char *fstype;
  struct inode *ip, *devi;

  if (argstr(0, &devf) < 0 || argstr(1, &path) < 0 || argstr(2, &fstype) < 0) {
    return -1;
  }

  if ((ip = namei(path)) == 0 || (devi = namei(devf)) == 0) {
    return -1;
  }

  struct filesystem_type *fs_t = getfs(fstype);

  if (fs_t == 0) {
    cprintf("FS type not found\n");
    return -1;
  }

  ip->iops->ilock(ip);
  devi->iops->ilock(devi);
  // we only can mount points over directories nodes
  if (ip->type != T_DIR && ip->ref > 1) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  // The device inode should be T_DEV
  if (devi->type != T_DEV) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  if (bdev_open(devi) != 0) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  if (devi->minor == 0 || devi->minor == ROOTDEV) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  // Add this to a list to retrieve the Filesystem type to current device
  if (putvfsonlist(devi->major, devi->minor, fs_t) == -1) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  int mounted = fs_t->ops->mount(devi, ip);

  if (mounted != 0) {
    ip->iops->iunlock(ip);
    devi->iops->iunlock(devi);
    return -1;
  }

  ip->type = T_MOUNT;

  ip->iops->iunlock(ip);
  devi->iops->iunlock(devi);

  return 0;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ip->iops->ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  ip->iops->iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int len;
  int major, minor;
  
  begin_op();
  if((len=argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ip->iops->ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  ip->iops->iunlock(ip);
  iput(proc->cwd);
  end_op();
  proc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      proc->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}
