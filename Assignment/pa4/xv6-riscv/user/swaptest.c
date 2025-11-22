#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/*
int main () {
	int a = -1, b = -1;
    swapstat(&a, &b);
    printf("a: %d, b: %d\n", a, b);
}
*/
int
main()
{
    int i;
    char *a;
    int read1, write1, read2, write2;

    // Allocate 4 pages
    a = sbrk(4096); a[0] = 'A';

    swapstat(&read1, &write1);

    // Allocate many more pages to force eviction
    for(i = 0; i < 5000; i++){
        char *x = sbrk(4096);
        if(x == (char*)-1)
        {
            printf("FAILED\n");
            break;
        }
        x[0] = 'X';
        if(i%1000 == 0) printf(".");
    }
    printf("\n");
    
    swapstat(&read2, &write2);
    if(write2 > write1)
        printf("SUCCESS: swapped out %d pages\n", write2-write1);
    else
        printf("Warining, No swapping\n");

    printf("Accessing...\n");
    char v = a[0];

    int read3, write3;
    swapstat(&read3, &write3);

    if(read3 > read2)
        printf("Success: Swapping in..\n");
    else
        printf("No swap in\n");

    printf("Got char: %d\n", v);
    exit(0);

}

