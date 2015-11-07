#ifndef XV6_DEVICE_H_
#define XV6_DEVICE_H_

// Block device switch table entry.
struct bdev_ops {
  int (*open)(int minor);
  int (*close)(int minor);
};

struct bdev {
  int major;
  struct bdev_ops *ops;
};

/* assumes size > 256 */
static inline unsigned int blksize_bits(unsigned int size)
{
  unsigned int bits = 8;
  do {
      bits++;
      size >>= 1;
    } while (size > 256);
  return bits;
}

#endif /* XV6_DEVICE_H_ */

