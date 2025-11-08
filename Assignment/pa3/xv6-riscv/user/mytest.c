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
    printf("TEST1\n");

    char *p = (char*) mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);

    if((long)p <= 0)
    {
        printf("FAILED\n");
        return 0;
    }

    strcpy(p, "hello mmap");
    if(strcmp(p, "hello mmap") == 0)
        printf("OK\n");
    else
        printf("FAILED\n");
        
    return 0;
}
