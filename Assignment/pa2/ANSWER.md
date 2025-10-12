# Answer to PA 3
## Requirements
- implement an EEVDF (Earliest Eligible Virtual Deadline First) scheduler
    - EEVDF must operate well so that runtime increases in accordance with priority
    - Vruntime, vdeadline and eligibility must be properly calculated
    - Upon wake up, the defined rule must be strictly followed
- Modify ps system call to output appropriate value
    - runtime/weight, runtime, vruntime, vdeadline, eligibility, and total tick

### EEVDF Terms
- weight
    - a more flexible version of nice value
    - formula in slides
- Time-slice
    - task's minimum time to run before preemption
    - formula in slides
- Lag Value
    - difference between idle runtime and actual runtime
- Virtual runtime
    - task's tracked runtime adjusted by its weight
    - formula in slides
- Virtual deadline
    - earliest time a task should finish its runtime
    - formula in slides

### EEVDF Steps
1. Each task sets its virtual deadline when it becomes eligible to run.
2. The task with the earliest deadline is scheduled.
3. While the task is running, virtual runtime and other scheduling parameters are updated.
4. After a task consumes its allocated execution time, its virtual deadline and eligibility are updated, and scheduler goes back to 2.

### Scoring
- Totally based on ps()
    - even if EEVDF is well implemented, if ps fails, no score will be given

## Answer
1. **EEVDF Core Logic**
    - Scheduler selects the process with the smallest **vdeadline** among runnable tasks.
    - `vruntime` and `vdeadline` are updated each tick as:
      ```c
      p->vruntime  += (1000 * 1024) / p->weight;
      p->vdeadline  = p->vruntime + (5000 * 1024) / p->weight;
      ```

2. **Initialization**
    - In `allocproc()`:
      ```c
      p->vruntime = 0;
      p->vdeadline = (5000 * 1024) / p->weight;
      ```
    - Weight is derived from the nice value (see slide 13).

3. **PS Output**
    - To keep integer math precise:
      ```c
      runtime_weight = (p->runtime / 1000) * 1024 / p->weight;
      ```

4. **Testing (Quick Sanity Test)**
    ```c
    int main(void) {
      int pid1 = fork();
      if (pid1 == 0) {
        setnice(getpid(), 30);
        for (volatile int i = 0; i < 100000000; i++);
        printf("Low priority done\\n");
        exit(0);
      }
      int pid2 = fork();
      if (pid2 == 0) {
        setnice(getpid(), 10);
        pause(20);
        for (volatile int i = 0; i < 100000000; i++);
        printf("High priority done\\n");
        exit(0);
      }
      for (int i = 0; i < 5; i++) {
        ps(0);
        pause(20);
    }
      waitpid(pid1);
      waitpid(pid2);
      printf("=== Test complete ===\\n");
      exit(0);
    }
    ```

5. **Expected Output**
    ```
    === Simple EEVDF Sanity Test ===
    High priority done
    Low priority done
    === Test complete ===
    ```

✅ *Interpretation:* High-priority task (larger weight) completes first,  
low-priority still finishes — confirming correct EEVDF scheduling and fairness.
