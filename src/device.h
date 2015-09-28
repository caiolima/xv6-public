// Block device switch table entry.
struct bdev_ops {
  int (*open)(int minor);
  int (*close)(int minor);
};

struct bdev {
  int major;
  struct bdev_ops *ops;
};

