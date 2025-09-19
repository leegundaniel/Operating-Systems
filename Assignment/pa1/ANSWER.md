# Answer to PA 2
## Requirements
- create 5 new system calls
    - [getnice](#getniceint-pid)
    - [setnice](#setniceint-pid-int-value)
    - [ps](#psint-pid)
    - [meminfo](#meminfo)
    - [waitpid](#waitpidint-pid)

### getnice(int pid)
- obtains nice value of the process `pid`

- return values:
    - nice value of target process
    - -1 on error
        - no process corresponding to the pid
      
### setnice(int pid, int value)
- sets the nice value of the process `pid`
  
- return values:
    - 0 on success
    - -1 on error
        - no process corresponding to the pid
      
### ps(int pid)
- prints out process(es)'s information, including <u>name, pid, state, and priority (nice value)</u>
    - if pid = 0, print out all processes' information
    - if pid exists, print out information of corresponding process
- no return value

### meminfo()
- prints available memory in bytes
- returns amount of free memory (in bytes) available in the system

### waitpid(int pid)
- suspends execution until the specified process terminates
- return values:
    - 0 on success
    - -1 on error
        - process does not exist
        - calling process does not have permission to wait


## Answer (proc.c)
1. first follow the system call preparation stage shown on pdf
2. find the process structure location (proc.h)
3. add priority (int nice) as a variable inside the structure
4. find the process initiating function / allocate (allocproc(void))
5. set default nice value to 20
        
### getnice
- similar to getpname code on pdf
1. go through the process list until pid is found using for loop
2. Once pid is found, return process->nice
3. if pid not found, return -1

### setnice
- similar to getnice code above
1. make sure `value` is in the range 0 ~ 39, if not, return -1
3. go through the process list until pid is found using for loop
4. once pid is found, set p->nice to `value` and return 0
5. if pid not found, return -1

### ps
- use static char list states found in `procdump(void)` to easily differentiate the different states
1. use for loop to check whether pid exists or if pid = 0
2. if pid does not exist, leave function
3. if pid exists, print pid information using printf
4. if pid = 0, use for loop to print all process information

### meminfo
- kalloc.c has several functions and data structures regarding memory allocation
1. declare and add a function in kalloc.c for finding the number of free pages in the memory list
2. run through free list and find the total number of free pages
3. return number of free pages
4. back to proc.c
5. call the function, which gives the number of free pages
6. PGSIZE = byte size of each page
7. return PGSIZE * the number of free pages

### waitpid
- similar function to `kwait` in `proc.c`
1. declare a myproc() to be used for waiting
2. acquire wait_lock
3. add infinite loop for the continuous waiting for a child fork to terminate
4. use another for loop to look for existence of pid
5. if pid is a zombie, return 0(successful waiting)
6. if pid not found or parent exited, return -1, error
7. use sleep function to keep the process sleeping 
