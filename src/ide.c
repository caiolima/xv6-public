// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "vfs.h"
#include "buf.h"
#include "device.h"
#include "s5.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havediskroot;
static void idestart(struct buf*);
static int ide_open(int minor);
static int ide_close(int minor);

struct bdev_ops ideops = {
  .open = &ide_open,
  .close = &ide_close
};

// define the ide device struct
struct bdev idedev = {
  .major = IDEMAJOR,
  .ops = &ideops
};

// IDE devices operations

/**
 * This operation verifies if the current disk n is attatched.
 **/
int
ide_open(int minor)
{
  int i;

  // Disk 0 is always attatched because the kernel is located there
  if (minor == 0)
    return 0;

  // It is the already attatched device
  if (minor == ROOTDEV)
    return havediskroot;

  if (minor >= 2) {
    // check if the disk is attatched
    outb(0x176, 0xe0 | (minor & 1<<4));
    for (i=0; i<1000; i++) {
      if (inb(0x177) != 0) {
        return 0;
      }
    }
  } else {
    outb(0x1f6, 0xe0 | (minor & 1<<4));
    for (i=0; i<1000; i++) {
      if (inb(0x1f7) != 0) {
        return 0;
      }
    }
  }

  return -1;
}

int
ide_close(int minor)
{
  return 0;
}

// Wait for IDE disk to become ready.
static int
idewait(int checkerr)
{
  int r;

  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY) 
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

void
ideinit(void)
{
  int i;

  initlock(&idelock, "ide");
  picenable(IRQ_IDE);
  ioapicenable(IRQ_IDE, ncpu - 1);
  picenable(IRQ_IDE + 1);
  ioapicenable(IRQ_IDE + 1, ncpu - 1);

  if (registerbdev(idedev) != 0 )
    panic("Register IDE device driver");

  idewait(0);

  // Check if disk 1 is present
  outb(0x1f6, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){
    if(inb(0x1f7) != 0){
      havediskroot = 1;
      break;
    }
  }

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));
}

// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  if(b->blockno >= FSSIZE)
    panic("incorrect blockno");

  // Verify if the device is from Primary or Secodary BUS
  int baseport;
  if (b->dev <= 1) {
    baseport = 0x1f0;
  } else {
    baseport = 0x170;
  }

  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;

  if (sector_per_block > 7) panic("idestart");

  idewait(0);

  if (b->dev <= 1) {
    outb(0x3f6, 0);  // generate interrupt to primary bus
  } else {
    outb(0x376, 0);  // generate interrupt to secondary bus
  }

  outb(baseport + 2, sector_per_block);  // number of sectors
  outb(baseport + 3, sector & 0xff);
  outb(baseport + 4, (sector >> 8) & 0xff);
  outb(baseport + 5, (sector >> 16) & 0xff);
  outb(baseport + 6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(baseport + 7, IDE_CMD_WRITE);
    outsl(baseport, b->data, BSIZE/4);
  } else {
    outb(baseport + 7, IDE_CMD_READ);
  }
}

// Interrupt handler.
void
ideintr(int secflag)
{
  int port;
  if (!secflag) {
    port = 0x1f0;
  } else {
    port = 0x170;
  }

  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);
  if((b = idequeue) == 0){
    release(&idelock);
    // cprintf("spurious IDE interrupt\n");
    return;
  }
  idequeue = b->qnext;

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    insl(port, b->data, BSIZE/4);

  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  struct buf **pp;

  if(!(b->flags & B_BUSY))
    panic("iderw: buf not busy");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havediskroot)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b)
    idestart(b);

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }

  release(&idelock);
}
