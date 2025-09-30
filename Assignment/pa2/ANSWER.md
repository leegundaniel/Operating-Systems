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
