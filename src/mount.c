#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{

  if(argc < 2){
    printf(2, "Usage: dev num, path...\n");
    exit();
  }

  if(mount(atoi(argv[0]), argv[1]) < 0){
    printf(2, "mount: failed to mounting device\n");
  }

  exit();
}

