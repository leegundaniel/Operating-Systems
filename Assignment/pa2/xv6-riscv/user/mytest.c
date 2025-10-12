#include "../kernel/types.h"
#include "user.h"
#include "../kernel/stat.h"


int main()
{
    /*
    int pid;

    printf("Testing getpname:\n");
    printf("1: ");
    getpname(1);

    printf(">>>Testing getnice and setnice:\n");
    printf("Initial nice value: %d\n", getnice(3));
    setnice(3, 10);
    printf("Nice value after setting: %d\n", getnice(3));

    printf(">>>Testing ps:\n");
    ps(0);
    
    printf(">>>Testing meminfo:\n");
    printf("Available memory: %d bytes\n",meminfo());
    
    printf(">>>Testing waitpid:\n");
    pid = fork();


    //fork failed: exit
    if(pid < 0)
    {
        printf("fork failed\n");
        exit(1);
    }
    //child
    else if(pid == 0)
    {
        printf("Child process: (pid: %d)\n", (int)getpid());
        exit(0);
    }
    //parent
    else
    {
        pause(5);
        printf("Parent process: (pid: %d)\n", (int)getpid());
        ps(0);
        int wait = waitpid(pid);
        if(wait < 0)
        {
            printf("Error: Child termination unsuccessful\n");
        }
        else
        {
            printf("Child terminated successfully\n");
        }
        printf("waitpid value: %d\n", wait);
    }
   
    exit(0);
    */

    /* Project 2 Test */
    int pid1 = fork();
    if (pid1 == 0) {
        setnice(getpid(), 30);    // low priority (small weight)
        for (volatile int i = 0; i < 100000000; i++);
        printf("Low priority done\n");
        exit(0);
    }

    int pid2 = fork();
    if (pid2 == 0) {
        setnice(getpid(), 10);    // high priority (large weight)
        pause(20);
        for (volatile int i = 0; i < 100000000; i++);
        printf("High priority done\n");
        exit(0);
    }

    for (int i = 0; i < 5; i++) {
        ps(0);
        pause(20);
    }

    waitpid(pid1);
    waitpid(pid2);
    ps(0);

    printf("=== Test complete ===\n");
    exit(0);
}

