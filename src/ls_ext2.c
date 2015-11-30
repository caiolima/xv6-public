#include "types.h"
#include "stat.h"
#include "user.h"
#include "vfs.h"
#include "ext2.h"

char*
fmtname(char *path)
{
  static char buf[EXT2_NAME_LEN + 1];
  char *p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if (strlen(p) >= EXT2_NAME_LEN)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', EXT2_NAME_LEN - strlen(p));

  return buf;
}

void
ls(char *path)
{
  char buf[512], *p, bufoff[1024];
  int fd, entryoff;
  struct ext2_dir_entry_2 de;
  struct stat st;

  if ((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type){
  case T_FILE:
    printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (1) {
      // Read the inode head
      if (!read(fd, &de, sizeof(de) - sizeof(char *)))
        break;

      printf(1, "Debug name length %d\n", de.name_len);
      read(fd, de.name, de.name_len); // Read the file name

      entryoff = sizeof(de) - sizeof(char *) + de.name_len - de.rec_len;
      if (entryoff > 0) {
        read(fd, bufoff, entryoff); // Read the file name
      }

      if (de.inode == 0)
        continue;
      memmove(p, de.name, de.name_len);
      p[de.name_len] = 0;

      if (stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      printf(1, "%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
