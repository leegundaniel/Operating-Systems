#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/fcntl.h"
#include "../kernel/memlayout.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/sleeplock.h"
#include "../kernel/fs.h"
#include "../kernel/syscall.h"

int main()
{
    printf("TEST 3\n");
    
    int fd = open("README", O_RDONLY);

    if(fd < 0){
        printf("CANT OPEN\n");
        exit(1);
    }

    uint64 addr = mmap(0, 4096, PROT_READ, 0, fd, 0);

    char *p = (char*)addr;
    printf("FIRST BYTE: %c\n", p[0]);
    printf("string: %.10s\n", p);

    printf("OK\n");

    return 0;
}
