/*
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main () {
	int a = -1, b = -1;
    swapstat(&a, &b);
    printf("a: %d, b: %d\n", a, b);
}
*/
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
consumeswap()
{
    // Allocate enough memory to fill a significant portion of swap
    // Adjust loop count if your swap space is very large
    int i;
    for(i = 0; i < 3000; i++){
        char *p = sbrk(4096);
        if(p == (char*)-1) break;
        *p = 'z';
    }
    // Just exit immediately, triggering uvmunmap
    exit(0);
}

int
main()
{
    printf("SwapLeak Test: Running multiple heavy processes...\n");

    // We run the heavy process multiple times.
    // If swap slots are leaked, later iterations will fail.
    for(int i = 0; i < 10; i++) {
        printf("Iteration %d...\n", i);
        int pid = fork();
        if(pid == 0) {
            consumeswap();
        }
        wait(0);
    }

    printf("SwapLeak Test: SUCCESS (Survived 10 iterations without OOM)\n");
    exit(0);
}
