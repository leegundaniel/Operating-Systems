#include "../kernel/types.h"
#include "user.h"
#include "../kernel/stat.h"

int main()
{
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
}
