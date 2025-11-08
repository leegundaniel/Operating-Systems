#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "defs.h"
#include "file.h"

#define MMAPBASE 0x40000000

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct mmap_area mmaps[64];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern int freepagespace(void); //int function to return number of free pages

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
  // initialize mmaps process
  for(int i = 0; i < 64; i++)
  {
        mmaps[i].p = 0;
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->nice = 20; //default priority value

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->nice = 20;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // duplicate mmap entries of parent
  for(i = 0; i < 64; i++) {
    // find mmaps that is the parent process
    if(mmaps[i].p == p) {
        // find empty space
        int j, found = 0;
        for(j = 0; j < 64; j++) {
            // find an empty array index
            if(mmaps[j].p == 0) {
                found = 1;
                break;
            }
        }
        // if no index found, error: kill child
        if(found == 0) {
            freeproc(np);
            release(&np->lock);
            return -1;
        }

        // copy parent's info
        mmaps[j].p = np;
        mmaps[j].addr = mmaps[i].addr;
        mmaps[j].length = mmaps[i].length;
        mmaps[j].prot = mmaps[i].prot;
        mmaps[j].flags = mmaps[i].flags;
        mmaps[j].offset = mmaps[i].offset;

        // copy file if exists
        if(mmaps[i].f)
            mmaps[j].f = filedup(mmaps[i].f);
        else
            mmaps[j].f = 0;

        // now checking for pages, ensuring to copy parent's pages
        for(uint64 virtual = mmaps[i].addr; virtual < mmaps[i].addr + mmaps[i].length; virtual += PGSIZE) {
            // walk through pte
            pte_t *pte = walk(p->pagetable, virtual, 0);
            if(pte == 0)
                continue;
            if((*pte & PTE_V) == 0)
                continue;
            
            // parent has a page mapped, then copy it
            uint64 physical = PTE2PA(*pte);
            char *page = kalloc();
            // if fail to alloc, release child
            if(page == 0) {
                freeproc(np);
                release(&np->lock);
                return -1;
            }
            // copy parent's page to child
            memmove(page, (char*)physical, PGSIZE);
            // mappages of child process, exit if error occurs
            if(mappages(np->pagetable, virtual, PGSIZE, (uint64)page, PTE_FLAGS(*pte)) < 0) {
                kfree(page);
                freeproc(np);
                release(&np->lock);
                return -1;
            }
        }
    }
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // close mmap regions
  for(int i = 0; i < 64; i++)
  {
    if(mmaps[i].p == p)
    {
        // unmap any mapped pages
        for(uint64 va = mmaps[i].addr; va < mmaps[i].addr + mmaps[i].length; va += PGSIZE)
        {   
            pte_t *pte = walk(p->pagetable, va, 0);
            if(pte && (*pte & PTE_V))
            {
                uint64 pa = PTE2PA(*pte);
                kfree((void*)pa);
                uvmunmap(p->pagetable, va, 1, 0);
            }
        }
        if(mmaps[i].f)
            fileclose(mmaps[i].f);
        memset(&mmaps[i], 0, sizeof(mmaps[i]));
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
         memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}


//getting process name
int
getpname(int pid)
{
    struct proc *p;

    //for loop to find pid
    for(p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        //if pid found, print name
        //return 0(success)
        if(p->pid == pid)
        {
            printf("%s\n", p->name);
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    //return -1 error
    return -1;
}

//get nice value of the specified pid
//success: nice value; error: -1
int
getnice(int pid)
{
    struct proc *p;

    //find pid in list
    //if found, return nice value
    for(p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if(p->pid == pid)
        {
            release(&p->lock);
            return p->nice;
        }
        release(&p->lock);
    }

    //if not found, return -1
    return -1;
}

//set nice value of the specified pid
//success: 0; error: -1
int
setnice(int pid, int value)
{
    struct proc *p;

    //make sure value in range 0~39
    if(value < 0 || value > 39)
    {
        return -1;
    }

    //find pid in list
    //if found, return nice value
    for(p = proc; p < &proc[NPROC]; p++)
    {    
        acquire(&p->lock); 
        //if process id = inputted pid
        if(p->pid == pid)
        {
            // return 0 (success)
            p->nice = value;
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }

    //if not found, return -1
    return -1;
}

//ps: print out pid list, if 0 is inputted, print entire list
// no return value
void
ps(int pid)
{
     static char *states[] = {
        [UNUSED]    "UNUSED  ",
        [USED]      "USED    ",
        [SLEEPING]  "SLEEPING",
        [RUNNABLE]  "RUNNABLE",
        [RUNNING]   "RUNNING ",
        [ZOMBIE]    "ZOMBIE  "
    };
    
    struct proc *p;
    int pid_exists = 0; //boolean to check if pid exists

    //check whether pid exists or is 0
    for(p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);

        if(p->pid == pid)
        {
            pid_exists = 1;
        }
        release(&p->lock);
    }
    if(pid == 0)
    {
        pid_exists = 1;
    }

    //if pid does not exist, exit function
    if(pid_exists == 0)
    {
        return;
    }
    
    //list template
    printf("name\tpid\tstate\t\tpriority\n");

    //print process info
    for(p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if(pid == 0 || p->pid == pid)
        {
            if(p->state == UNUSED)
            {
                release(&p->lock);
                continue;
            }
            printf("%s\t%d\t%s\t%d\n", p->name, p->pid, states[p->state], p->nice);
        }
        release(&p->lock);
    }

    return;

}

//return available memory in bytes
int
meminfo()
{
    //find the number of free pages
    int pages = freepagespace();

    //return amount of free memory in bytes
    return PGSIZE * pages;
}

//return 0 if process terminated successfully
// -1 if process does not exist or error
int
waitpid(int pid)
{
    struct proc *p;
    int pid_found;
    //current_p = current process
    struct proc *current_p = myproc();
   
    acquire(&wait_lock);
    //infinite loop
    for(;;)
    {
        pid_found = 0;

        //Scan process table for pid and check if it's a child
        for(p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            //check if this is the right process
            if(p->pid != pid)
            {
                release(&p->lock);
                continue;
            }

            pid_found = 1;

            //once process found, check if parent of found child is the current process
            if(p->parent != current_p)
            {
                release(&p->lock);
                release(&wait_lock);
                return -1;
            }
           
            //process is child of current process
            //check for zombie state
            if(p->state == ZOMBIE)
            {
                //free memory
                freeproc(p);
                release(&p->lock);
                release(&wait_lock);
                //return 0
                return 0;
            }
            release(&p->lock);

        }

        //if no such child or current process was killed, return error -1
        if(!pid_found || current_p->killed)
        {
            release(&wait_lock);
            return -1;
        }
        //sleep until the child exits
        sleep(current_p, &wait_lock);

    }
}

// memory mapping
uint64
mmap(uint64 addr, int length, int prot, int flags, int fd, int offset)
{
    // get current process and starting address
    struct proc *p = myproc();
    uint64 start_addr = addr + MMAPBASE;
    char* mem = 0;
    
    // ensure length is page aligned
    if(length <= 0 || (length % PGSIZE) != 0)
    {
        return 0;
    }
    
    // ensure values are page aligned
    if((addr % PGSIZE) != 0 || (start_addr % PGSIZE) != 0)
    {
        return 0;
    }

    struct file* f = 0;
    
    // error if not anonymous and fd = -1
    if(!(flags & MAP_ANONYMOUS) && (fd == -1))
    {
        return 0;
    }
    // error if anonymous but fd and offset have values
    if((flags & MAP_ANONYMOUS) && (fd != -1 || offset != 0))
    {
        return 0;
    }

    // if fd exists read file
    if(fd != -1)
    {
        f = p->ofile[fd];
    }

    // Protection of file and the prot of parameter are different
    if(f != 0)
    {
        // read protection error
        if(!(f->readable) && (prot & PROT_READ))
        {
            return 0;
        }

        // write protection error
        if(!(f->writable) && (prot & PROT_WRITE))
        {
            return 0;
        }
    }
    
    // find empty mmaps index
    int idx, found = 0;
    for(int i = 0; i < 64; i++)
    {
        // empty mmaps index found
        if(mmaps[i].p == 0)
        {
            idx = i;
            found = 1;
            break;
        }
    }
    // mmaps full error
    if(found == 0)
    {
        return 0;
    }

    // increment ref count
    if(f)
    {
        f = filedup(f);
    }

    // MAP POPULATE
    if(flags & MAP_POPULATE)
    {
        int off = offset;
        
        // for loop to map pages
        for(uint64 va = start_addr; va < start_addr + length; va += PGSIZE)
        {
            // kalloc, if mem = 0, kalloc failed
            if((mem = kalloc()) == 0)
            {
                return 0;
            }
            // set memory
            memset(mem, 0, PGSIZE);
               
            // file mapping
            if(!(flags & MAP_ANONYMOUS))
            {
                // save original offset, and store the offset currently required
                int offset_store = f->off;
                f->off = off;

                // read file
                fileread(f, (uint64)mem, PGSIZE);

                // restore file offset
                f->off = offset_store;
                
                off += PGSIZE;
                
            }

            // page permission
            int perm = PTE_U;
            if(prot & PROT_READ) perm |= PTE_R;
            if(prot & PROT_WRITE) perm |= (PTE_R | PTE_W);
            // map VA to PA 
            if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) < 0)
            {
                // if error, free pages
                kfree(mem);
                return 0;
            }
        }
    }
    
    // mmaps value updates
    mmaps[idx].f = f;
    mmaps[idx].addr = start_addr;
    mmaps[idx].length = length;
    mmaps[idx].offset = offset;
    mmaps[idx].prot = prot;
    mmaps[idx].flags = flags;
    mmaps[idx].p = p;
    
    
    return start_addr;
}

// memory unmapping
int munmap(uint64 addr)
{
    struct proc *p = myproc();
    int idx, found = 0;
    
    // return 0 if size is not page-aligned
    if(addr % PGSIZE != 0)
    {
        return 0;
    }

    // loop through mmaps array
    for(int i = 0; i < 64; i++)
    {
        // check if addr exists in array
        if(addr == mmaps[i].addr)
        {
            // make sure index in array is currently valid
            if(mmaps[i].p == p)
            {
                // if valid, set the new index
                idx = i;
                found = 1;
                break;
            }
        }
    }
    // if entry in array not found, error
    if(found == 0)
    {
        return -1;
    }

    // free mapped pages
    for(uint64 ptr = addr; ptr < addr + mmaps[idx].length; ptr += PGSIZE)
    {
        // walk through pagetable for pages
        pte_t *pte = walk(p->pagetable, ptr, 0);
        // pte is valid
        if(pte && (*pte & PTE_V))
        {
            // retrieve physical address
            uint64 pa = PTE2PA(*pte);
            // free physical address
            kfree((void*)pa);
            // unmap
            uvmunmap(p->pagetable, ptr, 1, 0);    
        }
    }

    // close file if it exists
    if(mmaps[idx].f)
    {
        fileclose(mmaps[idx].f);
    }
    // clear mmaps
    memset(&mmaps[idx], 0, sizeof(mmaps[idx]));
    // ensure p = 0
    mmaps[idx].p = 0;
    
    return 1;
}

int
freemem()
{
    return freepagespace();
}
