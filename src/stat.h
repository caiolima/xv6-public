
#ifndef XV6_STAT_H_
#define XV6_STAT_H_

#define T_DIR   1   // Directory
#define T_FILE  2   // File
#define T_DEV   3   // Device
#define T_MOUNT 4   // Mount Point

struct stat {
  short type;  // Type of file
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short nlink; // Number of links to file
  uint size;   // Size of file in bytes
};

#endif /* XV6_STAT_H_ */

