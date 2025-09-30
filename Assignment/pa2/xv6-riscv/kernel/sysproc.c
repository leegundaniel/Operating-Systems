#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//get process name
uint64
sys_getpname(void)
{
    int pid;

    argint(0, &pid);
    return getpname(pid);
}

// return nice value of pid
// -1 if error
uint64
sys_getnice(void)
{
    int pid;
    //get argument
    argint(0, &pid);

    return getnice(pid);
}

//set nice value of pid
// 0 for success, -1 on error
uint64
sys_setnice(void)
{
    int pid, value;
    //get arguments
    argint(0, &pid);
    argint(1, &value);

    return setnice(pid, value);
}

//print out process list
uint64
sys_ps(void)
{
    int pid;
    //get argument
    argint(0, &pid);
    
    ps(pid);
    return 0;
}

//meminfo returns amount of free memory
uint64
sys_meminfo(void)
{
    return meminfo();
}

//suspend execution until specified process terminates
uint64
sys_waitpid(void)
{
    int pid;
    //get argument
    argint(0, &pid);

    return waitpid(pid);
}
