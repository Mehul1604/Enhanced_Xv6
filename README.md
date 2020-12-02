
# **Enhanced Xv6 Operating System**

#### Mehul Mathur 2019101046

## Introduction
The Xv6 Operating System is a simple OS developed in C  for MIT's operating systems course in summer of 2006. In this assignment , a few more functionalities have been added to it. The Xv6 OS by default had a Round Robin CPU scheduler , but in this assignment FCFS , PBS and MLFQ schedulers have also been added. System calls waitx , setPriority and ps have also been added to complement these additions. 

### Added files
- ####  time.c - The time user command
**time < user command >**
This file is to include the user command "time" for the shell of the operating system. This command runs the command that is in the arguments and then displays its Running time on CPU and Waiting time in ready queues(multiple in MLFQ) in **ticks** (*100 ticks is equal to 1 second in this OS*).

---
- #### setPriority.c - The setPriority user command
**setPriority < new priority >  < pid >**
This file is to include the user command "setPriority" in the shell for the OS. This command sets the priority of the process identified by *pid* to *new priority*. If the process's priority is higher than before, then rescheduling is done and the context is passed back to the scheduler. (Due to preemptive nature)

---
- #### ps.c - The ps user command
**ps**
This file is to run the command that displays the current processes and their details. The details displayed by typing ps are:-
- PID - Process Pid
- Priority - Priority of the process (-1 if scheduler is not PBS)
- State - State of the process(EMBRYO , RUNNABLE , RUNNING , SLEEPING , ZOMBIE) (cant be unused)
- r_time - Running time in ticks
- w_time - Instantaneous waiting time in any queue (Zero if the process is running, sleeping or zombie)
- n_run - Number of times it is picked by cpu to run
- cur_q - The current queue the process is in (-1 if the scheduler is not MLFQ)
- q0 - Number of ticks while in queue 0 of MLFQ(-1 if the scheduler is not MLFQ) 
- q1 - Number of ticks while in queue 1 of MLFQ(-1 if the scheduler is not MLFQ) 
- q2 - Number of ticks while in queue 2 of MLFQ(-1 if the scheduler is not MLFQ) 
- q3 - Number of ticks while in queue 3 of MLFQ(-1 if the scheduler is not MLFQ) 
- q4 - Number of ticks while in queue 4 of MLFQ(-1 if the scheduler is not MLFQ) 
---
- #### benchmark.c - The benchmark test user program
**benchmark**
This is file is to include the benchmark test program. This program uses a lot of cpu time and I/O time so that it can properly test the schedulers. It creates 15 processes and makes each of them successively sleep more and more (sleep(200)). It runs an empty for loop from 0 to 10^8 to use cpu cycles. For PBS , it also has a call to setPriority syscall which decreases process->priority (gives them higher priority) for the processes having more I/O time.

--- 

### Added Structs/Variables

- #### #define NQUEUES 5 (number of queues for MLFQ) (in proc.h)
---
- #### #define THRESHOLD 30 (waiting time limit after which process is promoted to an upper queue in MLFQ) (in proc.h)
---
 - #### struct proc (in proc.h)
 This is the structure for every process. The *added fields* for this assignment are :- 
 - int ctime (Creation time of process in ticks)
 - int etime (End time(when it exits) time of process in ticks)
 - int rtime (Running time of process in ticks)
 - int tot_wait (Total waiting time in ticks the process has spent in ready queues). At every point when the process exits a queue (state changes from runnable to something else) , this is updated as *tot_wait += (ticks - queue_enter_time).*
 - int priority (Priority of the process. Default - 60 , Range - 0 to 100 inclusive). Lower the number more the priority
 - int runs (Number of time process ran on the cpu)
 - int queue (Queue the process is assigned to be added to Range 0 to 4 inclusive)
 - int cur_ticks - (The current number of ticks while in a particular queue - useful for MLFQ in demoting to lower queue)
 - int queue_enter_time (The time in ticks when process enters any queue). This is updated and set to *ticks* every time a process enters a queue(becomes RUNNABLE)
 - int ticks_per_q[NQUEUES] (Number of ticks in each queue in MLFQ)
 - struct proc* next (Pointer to the next process in the queue in MLFQ (0 if not MLFQ))
---
- #### struct proc* queue_arr[NQUEUES] ( declaration of the 5 queues in MLFQ) (in proc.h)
---

### Functions/System calls

- #### int waitx(int* rtime , int* wtime) system call
This system call is the same as their wait() system call except it assigns the total waiting time and run time of the process reaped to the addresses defined by the parameters rtime and wtime. 
**(rtime) = process->rtime*
**(wtime) = process->tot_wait* **(the wait time calculated here is total time process spent in ready queues and does not include sleeping time)**

---
- #### int set_priority(int new_priority , int pid) system call
This system call is used to change the process defined by pid to the new_priority. It returns the old priority of the process. If the priority is higher than before, the cpu is made to yield any process running on it for rescheduling to occur . (new_priority can be only between 0 and 100 inclusive)
*process->priority = new_priority*

---

- #### void update_runtime(void)
This functions increases the rtime field of the process by 1 every tick
*myproc()->rtime++*

---
- #### void update_cur_ticks(void)
This functions increases the cur_ticks field of the process by 1 every tick
*myproc()->cur_ticks++*

----

- #### void update_per_ticks(void)
This functions increases the ticks_per_q field of the process for its queue by 1 every tick
*myproc()->ticks_per_q[myproc() -> queue]++*

---

- #### void push(int q_ind , struct proc* p)
This is function to push a process in a queue whose index is q_ind. It performs a linked list type insert operation. It also resets cur_ticks to 0.

---

- #### void pop(int q_ind)
This is a function to pop out a process from the head of the queue it is in. 

---

- #### void age_adjust(void)
This function is to implement aging in MLFQ. When processes instantaneous wait time (ticks - queue_enter_time) becomes more than THRESHOLD (30) it is deleted from this queue and pushed at the tail of the upper queue. Note that queue_enter_time is updated to ticks here too.

---

- #### void ps() system call
This system call prints the necessary details of the process as mentioned above.

---

### SCHEDULERS 

- #### RR (Round Robin)
This scheduler runs a loop through the ptable loop and runs each of them for a quantum of one tick. This was the original scheduler implemented by xV6.

*Run time - 4 or 5 ticks
Total time ~ 3000 ticks*
(Average values)

---

- #### FCFS (First Come First Serve)
This scheduler runs a loop through the ptable once to select the process with the least creation time , and then runs that process. If 2 processes have the same creation time it just selects the first one it sees.

*Run time - 7 ticks
Total time ~ 4100 ticks*
(Average values)

---

- #### PBS (Priority Based Scheduling)
This scheduler runs a loop through the ptable and selects the minimum priority. Now it runs thru all the processes with this minimum priority and runs over them in a Round Robin fashion. In this round robin loop tho , it also keeps checking in a loop if another process of even lower priority has arrived. This ensures preemption

*Run time - 4 or 5 ticks
Total time ~ 3100 ticks*
(Average values)

---

- #### MLFQ (Multi level Feedback Queue)
This scheduler makes use of 5 queues. A process starts out in queue 0 , and is allowed a quantum of 1 tick. If it exceeds that , it is pushed into the queue below. Queue 1 is allowed 2 ticks , queue 2 is allowed 4 ticks , queue 3 is allowed 8 ticks and queue 4 is allowed 16 ticks. Each time a process exceeds this , it is pushed to back of the below queue. Except in the last queue , where it runs in round robin fashion. 
A process can also however be promoted to the upper queue if it has been waiting for more than 30 ticks in any queue. (Aging)

**Note - When a process goes into sleeping state and then wants to return to a queue , it returns to the same queue. This can be exploited by the process as the process who has only a few ticks left , can still claim the same queue for its completion without demotion. This makes it exploit time for all other processes**
 *Run time - 0 or 2 ticks
Total time ~ 3050 ticks*
(Average values)

#### Comparision
As we can see from the total time taken by all the schedulers :-
RR is the fastest one in terms of total time and also gives fair run time to the process that forks all other processes in the bench mark.
MLFQ and PBS are not far behind as they also complete in almost the same time or a little bit lower.
FCFS is the slowest one as it increases the overall turnaround time due to convoy effect.

from most time to least: -
FCFS > PBS >= MLFQ >= RR

Note - The waiting time is less in all as the time command measures only for the process which forks out all the other children not the child processes themselves. The child processes themselves can have high wait times.

#### Macros and how to run
- Run make clean
- Run make qemu-nox SCHEDULER=< name of scheduler(RR , FCFS , PBS , MLFQ) >
- If no name , then RR is run









