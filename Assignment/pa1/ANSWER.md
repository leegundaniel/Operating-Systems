# Answer to PA 2
## Requirements
- create 5 new system calls
    - [getnice](#getnice)
    - [setnice](#setnice)
    - [ps](#ps)
    - [meminfo](#meminfo)
    - [waitpid](#waitpid)

### getnice
- getnice(int pid)
- obtains nice value of the process `pid`

- return values:
    - nice value of target process
    - -1 on error
        - no process corresponding to the pid
      
### setnice
- setnice(int pid, int value)
- sets the nice value of the process `pid`
  
- return values:
    - 0 on success
    - -1 on error
        - no process corresponding to the pid
      
### ps
- ps(int pid)
- prints out process(es)'s information, including <u>name, pid, state, and priority (nice value)</u>
    - if pid = 0, print out all processes' information
    - if pid exists, print out information of corresponding process
- no return value

### meminfo
- meminfo()
- prints available memory in bytes
- returns amount of free memory (in bytes) available in the system

### waitpid
- waitpid(int pid)
- suspends execution until the specified process terminates
- return values:
    - 0 on success
    - -1 on error
        - process does not exist
        - calling process does not have permission to wait
