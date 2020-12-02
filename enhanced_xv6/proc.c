#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
uint ticks;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

void
q_initialize(void)
{
	acquire(&ptable.lock);

	for(int i=0;i<NQUEUES;i++)
		queue_arr[i] = 0;

	release(&ptable.lock);
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->ctime = ticks;
  p->rtime = 0;
  p->tot_wait = 0;
  p->priority = 60;
  p->runs = 0;
  p->cur_ticks = 0;
  p->queue = -1;
  for(int i=0;i<NQUEUES;i++)
  {
  	p->ticks_per_q[i] = 0;
  }
  p->next = 0;


  //cprintf("Process %d created with creation time %d\n" , p->pid , p->ctime);

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  p->queue_enter_time = ticks;
  #ifdef MLFQ
  	p->queue = 0;
  	push(p->queue , p);
  #endif

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->queue_enter_time = ticks;
  #ifdef MLFQ
  	np->queue = 0;
  	push(np->queue , np);
  #endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  if(curproc->state == RUNNABLE)
  {
  	p->tot_wait += (ticks - curproc->queue_enter_time);
  }

  curproc->state = ZOMBIE;
  curproc->etime = ticks;
  curproc->queue = -1;
  //cprintf("Process %d exited and left us at end time %d\n" , curproc->pid , curproc->etime);
  //cprintf("Process %d ended with end time %d , run time %d and iotime %d\n" , curproc->pid , curproc->etime , curproc->rtime , curproc->iotime);
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
waitx(int* wtime , int* rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.

      	//putting the required values
      	*rtime = p->rtime;
	    *wtime = p->tot_wait;

        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

void
update_runtime()
{
	acquire(&ptable.lock);

	if(myproc())
	{
	  if(myproc()->state == RUNNING)
	  {
	  	 myproc()->rtime++;
	  	 //cprintf("Process %d is running\n" , myproc()->pid);
	  }
	}

	release(&ptable.lock);
}



void
update_cur_ticks()
{
	acquire(&ptable.lock);

	if(myproc())
	{
	  if(myproc()->state == RUNNING)
	  {
	  	 myproc()->cur_ticks++;
	  }
	}

	release(&ptable.lock);
}

void
update_per_ticks()
{
	acquire(&ptable.lock);

	if(myproc())
	{
	  if(myproc()->state == RUNNING)
	  {
	  	 myproc()->ticks_per_q[myproc()->queue]++;
	  }
	}

	release(&ptable.lock);
}

//QUEUE RELATED FUNCTIONS

void
push(int q_ind , struct proc* p)
{
	

	if(p == 0)
		panic("Queuing error!");

	p->cur_ticks = 0;
	//p->queue_enter_time = ticks;

	p->next = 0;
	if(queue_arr[q_ind] == 0)
		queue_arr[q_ind] = p;
	else
	{
		struct proc* run;
		run = queue_arr[q_ind];
		while(run->next != 0)
			run = run->next;

		run->next = p;
	}
}

void
pop(int q_ind)
{
	struct proc* temp = queue_arr[q_ind];
	queue_arr[q_ind] = queue_arr[q_ind]->next;
	temp->next = 0;

}

void
age_adjust()
{
	//struct proc* upper_head;
	struct proc* lower_head;
	struct proc* run;
	for(int i=1;i<NQUEUES;i++)
	{
		//upper_head = queue_arr[i-1];
		lower_head = queue_arr[i];
		if(lower_head == 0)
			continue;

		run = lower_head;
		struct proc* prev_run;
		int waiting_time;
		struct proc* dupl;
		while(run != 0)
		{
			waiting_time = ticks - run->queue_enter_time;
			if(waiting_time > THRESHOLD)
			{
				//cprintf("Process %d is promoted from queue %d to queue %d\n" , run->pid , run->queue)
				dupl = run->next;
				if(run == lower_head)
				{
					pop(i);

					run->queue_enter_time = ticks;
					run->queue--;
					push(i-1 , run);
					run = dupl;
				}
				else
				{
					prev_run = lower_head;
					while(prev_run->next != run)
						prev_run = prev_run->next;

					prev_run->next = run->next;
					run->next = 0;

					run->queue_enter_time = ticks;
					run->queue--;
					push(i-1 , run);
					run = dupl;
				}

			}
			else
				run = run->next;
		}

	}
}


int
set_priority(int new_priority , int pid)
{

	if(new_priority < 0 && new_priority > 100)
	{
		cprintf("Invalid priority\n");
		return -1;
	}

	acquire(&ptable.lock);
	int flag = 0;
	int old_priority;
	struct proc* p;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if(p->pid == pid)
		{
			old_priority = p->pid;
			p->priority = new_priority;
			flag = 1;
			break;
		}
	}

	release(&ptable.lock);
	if(flag == 0)
	{
		cprintf("Invalid PID\n");
		return -1;
	}

	if(p->priority < old_priority)
		yield();

	return old_priority;

}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  #ifdef FCFS
	  cprintf("FCFS scheduler\n");


	  for(;;){
	    // Enable interrupts on this processor.
	    
	    sti();

	    struct proc *minp = 0;
	    // Loop over process table looking for process to run.
	    acquire(&ptable.lock);
	    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	      if(p->state != RUNNABLE)
	        continue;



	      // Switch to chosen process.  It is the process's job
	      // to release ptable.lock and then reacquire it
	      // before jumping back to us.

	      if(minp == 0)
	      {
	      	minp = p;
	      }
	      else
	      {
	      	if(p->ctime < minp->ctime)
	      		minp = p;
	      }
 
	    }

	    p = minp;

	    if(p != 0)
	    {
	    	c->proc = p;
	    	

		    switchuvm(p);
		    p->state = RUNNING;
		    p->runs++;
		    p->tot_wait += (ticks - p->queue_enter_time);
		    //p->wtime = 0;
		    //cprintf("Process %d with creation time %d is picked to run\n" , p->pid , p->ctime);
		      //cprintf("Process %d is running\n" , p->pid);

		    swtch(&(c->scheduler), p->context);
		      //cprintf("Process %d has yielded to scheduler\n" , p->pid);
		    switchkvm();

		      // Process is done running for now.
		      // It should have changed its p->state before coming back.
		    c->proc = 0;
	    }
	    

	    release(&ptable.lock);

	  }

  #endif

  #ifdef RR
	  cprintf("RR scheduler\n");
	  for(;;){
	    // Enable interrupts on this processor.
	    
	    sti();

	    // Loop over process table looking for process to run.
	    acquire(&ptable.lock);
	    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	      if(p->state != RUNNABLE)
	        continue;

	      // Switch to chosen process.  It is the process's job
	      // to release ptable.lock and then reacquire it
	      // before jumping back to us.
	      c->proc = p;
	      switchuvm(p);
	      p->state = RUNNING;
	      p->runs++;
	      p->tot_wait += (ticks - p->queue_enter_time);
	      //cprintf("Process %d with creation time %d is picked to run\n" , p->pid , p->ctime);
	      //cprintf("Process %d is running\n" , p->pid);

	      swtch(&(c->scheduler), p->context);
	      //cprintf("Process %d has yielded to scheduler\n" , p->pid);
	      switchkvm();

	      // Process is done running for now.
	      // It should have changed its p->state before coming back.
	      c->proc = 0;
	    }
	    release(&ptable.lock);

	  }

  #endif

  
  #ifdef PBS
	  cprintf("PBS scheduler\n");


	  for(;;){
	    // Enable interrupts on this processor.
	    
	    sti();

		int min_pr = -1;
		// int min_chances = -1;
		//struct proc* chosen_proc = 0;

		acquire(&ptable.lock);
	     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	       if(p->state != RUNNABLE)
	         continue;

	     	if(min_pr == -1)
	     		min_pr = p->priority;
	     	else
	     	{
	     		if(p->priority < min_pr)
	     			min_pr = p->priority;
	     	}
	 	}

	 	if(min_pr != -1)
	 	{
		 	for(p=ptable.proc; p < &ptable.proc[NPROC];p++)
		 	{
		 		
		 		struct proc* q;
		 		int preempt = 0;
		 		for(q = ptable.proc; q < &ptable.proc[NPROC]; q++)
		 		{
		 			if(q->state != RUNNABLE)
	        			continue;

	        		if(q->priority < min_pr)
	        		{
	        			preempt = 1;
	        			break;
	        		}
		 		}

		 		if(preempt)
		 			break;

		 		if(p->state != RUNNABLE)
	        		continue;

		 		if(p->priority == min_pr)
		 		{
		 			c->proc = p;
		 			switchuvm(p);
		     		p->state = RUNNING;
		     		p->runs++;
		     		p->tot_wait += (ticks - p->queue_enter_time);

		     		//cprintf("Process %d with priority %d is picked to run\n" , p->pid , p->priority);
		      		//cprintf("Process %d is running\n" , p->pid);

		  		    swtch(&(c->scheduler), p->context);
		      		//cprintf("Process %d has yielded to scheduler\n" , p->pid);
		    		switchkvm();

		      		// Process is done running for now.
		      		// It should have changed its p->state before coming back.
		    		c->proc = 0;

		 		}
		 	}
	 	}

	 	release(&ptable.lock);

	  }

  #endif

  #ifdef MLFQ
	  cprintf("MLFQ scheduler\n");
	  for(;;){
	    // Enable interrupts on this processor.
	    
	    sti();

	    p = 0;

	    // Loop over process table looking for process to run.
	    acquire(&ptable.lock);

	      age_adjust(); //shift process in appropriate queues
	    
	      for(int i=0;i<NQUEUES;i++)
	      {
	      	if(queue_arr[i] == 0)
	      		continue;

	      	p = queue_arr[i];

	      	pop(i);
	      	break;
	      }
	      // Switch to chosen process.  It is the process's job
	      // to release ptable.lock and then reacquire it
	      // before jumping back to us.

	      if(p!=0 && p->state == RUNNABLE)
	      {
		      c->proc = p;
		      switchuvm(p);
		      p->state = RUNNING;
		      p->runs++;
		      //cprintf("Process %d picked from queue %d to run\n",p->pid , p->queue);
		      p->tot_wait += (ticks - p->queue_enter_time);
		      //cprintf("Process %d with creation time %d is picked to run\n" , p->pid , p->ctime);
		      //cprintf("Process %d is running\n" , p->pid);


		      swtch(&(c->scheduler), p->context);
		      //cprintf("Process %d has yielded to scheduler\n" , p->pid);
		      switchkvm();

		      // if(p->state == RUNNABLE)
		      // {
		      // 	push(p->queue , p);
		      // }

		      // Process is done running for now.
		      // It should have changed its p->state before coming back.
		      c->proc = 0;
		  }
	    
	    release(&ptable.lock);

	  }

  #endif

  

}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->queue_enter_time = ticks;
  #ifdef MLFQ
    push(myproc()->queue , myproc());
  #endif
  //update entry
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  //insta wait time
  if(p->state == RUNNABLE)
  {
  	p->tot_wait += (ticks - p->queue_enter_time);
  }
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == SLEEPING && p->chan == chan)
    {
      p->state = RUNNABLE;
      p->queue_enter_time = ticks;
      //update entry
      #ifdef MLFQ
      	push(p->queue , p);
      #endif
    }

  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
      {
        p->state = RUNNABLE;
        p->queue_enter_time = ticks;
        //update entry
        #ifdef MLFQ
        	push(p->queue , p);
        #endif
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
ps(void)
{
  static char *states[] = {
  [UNUSED]    "Unused",
  [EMBRYO]    "Embryo",
  [SLEEPING]  "Sleeping ",
  [RUNNABLE]  "Runnable",
  [RUNNING]   "Running",
  [ZOMBIE]    "Zombie"
  };
  //int i;
  struct proc *p;
  int pid;
  char *state;
  int priority;
  int runtime;
  int insta_wait_time;
  int n_run;
  int cur_q;
  int q[5];

  cprintf("\n");
  cprintf("PID\t\tPriority\t\tState\t\tr_time\t\tw_time\t\tn_run\t\tcur_q\t\tq0\t\tq1\t\tq2\t\tq3\t\tq4\n\n");
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;

  	pid = p->pid;
  	priority = -1;
  	#ifdef PBS
  		priority = p->priority;
  	#endif

    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

  	runtime = p->rtime;
  	if(p->state == RUNNABLE)
  		insta_wait_time = (ticks - p->queue_enter_time);
  	else
  		insta_wait_time = 0;

  	n_run = p->runs;

  	cur_q = -1;
  	for(int i=0;i<5;i++)
  		q[i] = -1;

  	#ifdef MLFQ
  		if(p->state == RUNNABLE)
  			cur_q = p->queue;
  		else
  			cur_q = -1;

  		for(int i=0;i<5;i++)
  			q[i] = p->ticks_per_q[i];

  	#endif

  	if(p->state != RUNNING)
  		cprintf("%d\t\t%d\t\t%s\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\n\n" , pid , priority , state , runtime , insta_wait_time , n_run , cur_q , q[0] , q[1] , q[2] , q[3] , q[4]);
  	else
  		cprintf("%d\t\t%d\t\t%s \t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\n\n" , pid , priority , state , runtime , insta_wait_time , n_run , cur_q , q[0] , q[1] , q[2] , q[3] , q[4]);

  }

  release(&ptable.lock);

  return 0;
}
