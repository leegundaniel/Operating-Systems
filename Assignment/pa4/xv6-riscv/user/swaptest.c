#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main () {
	int a = -1, b = -1;
    swapstat(&a, &b);
    printf("a: %d, b: %d\n", a, b);

    int size = 4096;
    for(int i = 0; i < 200000; i++)
    {
        char *p = malloc(size);
        if(!p)
        {
            printf("malloc failed at %d\n", i);
            exit(0);

        }
        p[0] = 'A';
    }

    printf("FINISHED\n");
    exit(0);



}
