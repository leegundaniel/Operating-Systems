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
#include "kernel/fs.h"
#include "kernel/fcntl.h"

// PDF Req: void swapstat(int* nr_sectors_read, int* nr_sectors_write) [cite: 132]
void swapstat(int *r, int *w); 

#define PGSIZE 4096
#define MB (1024 * 1024)

// Adjust this based on your PHYSTOP. 
// If PHYSTOP is 32MB, RAM is ~20-24MB usable. 
// We need to allocate MORE than RAM to force swap.
#define ALLOC_SIZE (10 * MB) 

void print_stats() {
    int r, w;
    swapstat(&r, &w);
    printf("[Stats] Swap Reads: %d, Swap Writes: %d\n", r, w);
}

// ============================================================
// TEST 1: Deallocation Verification (sbrk negative)
// PDF Req: "When a user virtual memory page is deallocated... 
// Swapped-out pages should also be cleared" 
// ============================================================
void test_dealloc() {
    printf("\n=== TEST 1: Deallocation (sbrk shrink) ===\n");
    print_stats();

    printf("1. Allocating huge memory to force swap...\n");
    int num_pages = 3000; // ~12MB
    char *mem = sbrk(num_pages * PGSIZE);

    if (mem == (char*)-1) {
        printf("FAIL: sbrk failed initial alloc.\n");
        exit(1);
    }

    // Touch pages to force physical allocation & swapping
    for(int i=0; i<num_pages; i++) {
        mem[i * PGSIZE] = 'X';
        if(i % 500 == 0) printf(".");
    }
    printf("\nAllocated & Touched.\n");

    printf("2. Shrinking memory (sbrk negative)...\n");
    // This triggers uvmdealloc -> uvmunmap.
    // If you implemented uvmunmap correctly, this frees swap slots.
    if(sbrk(-(num_pages * PGSIZE)) == (char*)-1) {
        printf("FAIL: sbrk shrink failed.\n");
        exit(1);
    }

    printf("3. Re-allocating memory...\n");
    // If swap slots were NOT freed, this allocation might fail 
    // (depending on how close we are to SWAPMAX).
    char *mem2 = sbrk(num_pages * PGSIZE);
    if(mem2 == (char*)-1) {
        printf("FAILURE: Could not re-allocate. Swap slots were likely leaked!\n");
        exit(1);
    }

    // Verify we can write to it
    for(int i=0; i<num_pages; i++) {
        mem2[i * PGSIZE] = 'Y';
    }
    
    printf("SUCCESS: Deallocation correctly freed resources.\n");
    // Cleanup
    sbrk(-(num_pages * PGSIZE));
}

// ============================================================
// TEST 2: Cycle Leak Test (Persistence Check)
// Verifies that process exit cleans up swap slots.
// ============================================================
void test_leak_cycle() {
    printf("\n=== TEST 2: Cycle Leak Test ===\n");
    
    int cycles = 3;
    int pages_per_cycle = 2000; // ~16MB per child

    for(int i=1; i<=cycles; i++) {
        printf("Cycle %d/%d...\n", i, cycles);
        int pid = fork();
        
        if(pid == 0) {
            // Child tries to eat 16MB
            char *mem = sbrk(pages_per_cycle * PGSIZE);
            if(mem == (char*)-1) {
                printf("Child OOM in cycle %d! Leak detected?\n", i);
                exit(1);
            }
            // Touch to force swap
            for(int j=0; j<pages_per_cycle; j++) {
                mem[j * PGSIZE] = (char)(j%256);
            }
            exit(0); // uvmunmap called here
        } else {
            int status;
            wait(&status);
            printf("Cycle %d: child %d exited with status %d\n", i, pid, status);
            if(status != 0) {
                printf("FAILURE: Child crashed/OOM in cycle %d.\n", i);
                exit(1);
            }
        }
    }
    printf("SUCCESS: 3 Cycles of heavy allocation/exit completed. No leaks.\n");
}

// ============================================================
// TEST 3: Data Integrity with Fork (The "Diff" Test)
// Verifies uvmcopy swap-in logic and COW/Separation
// ============================================================
void test_fork_data() {
    printf("\n=== TEST 3: Fork Data Integrity ===\n");

    int size = 2000 * PGSIZE; // ~8MB
    char *mem = malloc(size);
    if(!mem) { printf("Malloc failed.\n"); exit(1); }

    printf("Parent filling memory...\n");
    for(int i=0; i<size; i+=PGSIZE) mem[i] = 'P'; // P for Parent

    int pid = fork();
    if(pid == 0) {
        // Child
        printf("Child checking data...\n");
        for(int i=0; i<size; i+=PGSIZE) {
            if(mem[i] != 'P') {
                printf("FAIL: Child read wrong data at %d\n", i);
                exit(1);
            }
            mem[i] = 'C'; // C for Child
        }
        printf("Child modified data. Exiting.\n");
        exit(0);
    } else {
        wait(0);
        printf("Parent checking data consistency...\n");
        for(int i=0; i<size; i+=PGSIZE) {
            if(mem[i] != 'P') {
                printf("FAIL: Parent memory overwritten by Child at %d!\n", i);
                exit(1);
            }
        }
        printf("SUCCESS: Parent/Child data is isolated and correct.\n");
    }
    free(mem);
}

int main(int argc, char *argv[]) {
    printf("Starting Comprehensive Project 4 Tests...\n");
    
    test_dealloc();
    test_leak_cycle();
    test_fork_data();

    printf("\nALL COMPREHENSIVE TESTS PASSED.\n");
    exit(0);
}
