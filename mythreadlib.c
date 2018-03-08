#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
TCB* schedulerRR ();
void activator();
void timer_interrupt(int sig);
void network_interrupt(int sig);

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue; /*The queue which will have all the threads*/

static TCB* running; /* Current running thread */
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;
static int count = 0; /*to know in what RR tick are we*/

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
  int i;

  tqueue = queue_new(); //I initialize the queue

  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }

  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;

  if(idle.run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }

  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;

  makecontext(&idle.run_env, idle_function, 1);

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;

  if(getcontext(&t_state[0].run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }

  /*initialize all the position in the table process
   * except for the first one*/
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize network and clock interrupts */
  init_network_interrupt();
  init_interrupt();
}


/* Create and initialize a new thread with body fun_addr and one integer argument
 * It returns the position in the array of the new thread*/
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;

  if (!init) /*if the library is not initialized*/
  {
    init_mythreadlib();
    init=1;
  }
  for (i=0; i<N; i++) /*search for a free position in the thread array*/
  {
      if (t_state[i].state == FREE) break;
  }
  if (i == N) /*the last position is not a valid one*/
  {
      return(-1);
  }

	/*initialize the context of the thread which is going to be created*/
  if(getcontext(&t_state[i].run_env) == -1)
	{
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }

	/*initialize the fields of the thread*/
  t_state[i].state = INIT;
  t_state[i].priority = priority; /*given by input to this method*/
  t_state[i].function = fun_addr; /*given by input to this method*/
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL)
	{
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  t_state[i].ticks = 3; /*inventado por mi*/

  enqueue(tqueue, &t_state[i]); /*enqueue the thread in the queue of threads*/
  makecontext(&t_state[i].run_env, fun_addr, 1); /*create the new context*/
  printf("\t\tQueue after inserting\n");
  queue_print(tqueue);
  return i;
} /****** End my_thread_create() ******/

/* Read network syscall */
int read_network()
{
   return 1;
}

/* Network interrupt  */
void network_interrupt(int sig)
{
}

/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid(); /*get the id of the current thread*/

  printf("*** THREAD %d FINISHED\n", tid);

  t_state[tid].state = FREE; /*change the position of the array to free*/
  free(t_state[tid].run_env.uc_stack.ss_sp);

  TCB* next = schedulerRR(tid); /*get the next thread to be executed*/
  activator_FIFO(next); /*perform the context switch*/
}

/* We change the thread to the next one */
void mythread_next() {
  int tid = mythread_gettid(); /*get the id of the current thread*/

  printf("*** THREAD %d NO MORE TIME (ticks remaining: %i)\n", tid, t_state[current].ticks);

	TCB* next = schedulerRR(tid); /*get the next thread to be executed*/
  activator_RR(&t_state[tid], next); /*perform the context switch*/
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();
  return t_state[tid].priority;
}

/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/*It substract 1 tick and it checks if there is no more ticks, returning 0*/
int tick_minus(){
  int tid = mythread_gettid(); /*get the id of the current thread*/
  t_state[tid].ticks--; /* store the remaining ticks*/
  if(t_state[tid].ticks < 1) /*check if the thread has finished its execution's time*/
	{
    return 0; /*the time for this process has finished*/
  }
  return 1; /*the process has execution's time left*/
}

/*It returns the number of remaining ticks (only for debugging)*/
int getTicks(){
  int tid = mythread_gettid();//I get the ID of the thread
  return t_state[tid].ticks;//I return the remaining ticks
}

/* FIFO scheduler
 * the new thread to be executed is returned*/
TCB* scheduler(int id){
  int i;
  for(i=0; i<N; i++) /*I check all the positions of the thread's array*/
	{
    if (t_state[i].state == INIT && t_state[i].tid != id) /*if it an available thread*/
 		{																											/*and it is not the current one */
        current = i; /*the new current thread it is the one found*/
	      return &t_state[i]; /*the entry of the array is returned*/
    }
  }
  printf("mythread_free: No thread in the system\nExiting...\n");
  exit(1);
}

/* RR scheduler
 * the new thread to be executed is returned*/
TCB* schedulerRR(int id){
  int i, aux = 0; /*aux is calculated to know which is the next thread in the correct order*/
  for(i=0; i<N; i++) /*I check all the positions of the thread's array*/
	{
    aux = (i + id) % N; /*calculate the next thread*/
    if (t_state[aux].state == INIT && t_state[aux].tid != id) /*if it an available thread*/
 		{																											/*and it is not the current one */
        current = aux; /*the new current thread it is the one found*/
	      return &t_state[aux]; /*the entry of the array is returned*/
    }
  }
  printf("mythread_free: No thread in the system\nExiting...\n");
  exit(1);
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
	if(PRINT == 1) printf ("Thread %d with priority %d\t remaining ticks %i\n", mythread_gettid(), mythread_getpriority(0), getTicks());
	if(tick_minus() == 0){ /*checking if the thread has finished its execution*/
		mythread_exit(); /*I finish the thread*/
		return;
	}
	count ++;
	if(count == QUANTUM_TICKS) /*RR time slice consumed*/
	{
		count = 0; /*restore the count*/
		mythread_next(); /*take the next thread and store the current one in the queue*/
	}
	return;
}

void activator_RR(TCB* actual, TCB* next){
  printf("We re going to swap %i with %i ticks to %i with %i ticks\n", actual-> tid, actual -> ticks, next -> tid, next -> ticks);
	/*NO SE PORQUE FUNCIONA CON SET Y SIN SWAP*/
	setcontext (&(next->run_env));
	//if(swapcontext (&(actual->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/
	return;
  }

/* Activator */
void activator_FIFO(TCB* next){
  setcontext (&(next->run_env));
  printf("mythread_free: After setcontext, should never get here!!...\n");
}
