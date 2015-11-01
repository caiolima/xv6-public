#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{

  if(argc < 4){
    printf(2, "Usage: dev num, path, fs-type...\n");
    exit();
  }

  if(mount(argv[1], argv[2], argv[3]) < 0){
    printf(2, "mount: failed to mounting device\n");
  }

  exit();
}

