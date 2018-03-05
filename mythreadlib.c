#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void network_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];
static struct queue *tqueue;//The queue which will have all the threads

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
  int i;
  tqueue = queue_new();//I initialize the queue
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
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
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize network and clock interrupts */
  init_network_interrupt();
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;

  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  t_state[i].ticks = 5;
  enqueue(tqueue, &t_state[i]);
  makecontext(&t_state[i].run_env, fun_addr, 1);
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
  int tid = mythread_gettid();

  printf("*** THREAD %d FINISHED\n", tid);
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  TCB* next = scheduler(tid);
  activator_FIFO(next);
}

/* We change the thread to the next one */
void mythread_next() {
  int tid = mythread_gettid();

  printf("*** THREAD %d NO MORE TIME\n", tid);
  int newTicks = t_state[tid].ticks - QUANTUM_TICKS;//I substract the ticks used in this slot to the remaining ones
  if(newTicks < 1){//I check if the process has finished
    mythread_exit();//It has finish so we exit the process
  }
  t_state[tid].ticks = newTicks;//I store the remaining ticks

  //TCB* next = t_state[tid] -> next;//Hay que hacer una puta queue
  TCB* next = scheduler(tid);
  activator_RR(&t_state[tid], next);
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
  int tid = mythread_gettid();//I get the ID of the thread
  t_state[tid].ticks--;//I store the remaining ticks
  if(t_state[tid].ticks < 1){//I check if there is no more time
    return 0;//The time for this process has finished
  }
  return 1;
}

/*It returns the number of remaining ticks (only for debugging)*/
int getTicks(){
  int tid = mythread_gettid();//I get the ID of the thread
  return t_state[tid].ticks;//I return the remaining ticks
}

/* FIFO para alta prioridad, RR para baja*/
TCB* scheduler(int id){
  int i;
  for(i=0; i<N; i++){
    if (t_state[i].state == INIT && t_state[i].tid != id) {
        current = i;
	      return &t_state[i];
    }
  }
  printf("mythread_free: No thread in the system\nExiting...\n");
  exit(1);
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
}

void activator_RR(TCB* actual, TCB* next){
  printf("We re going to swap %i to %i\n", actual-> tid, next -> tid);
  if(swapcontext (&(actual->run_env), &(next->run_env)) == -1) printf("Swap error");//I change the contex to the next thread
  }

/* Activator */
void activator_FIFO(TCB* next){
  setcontext (&(next->run_env));
  printf("mythread_free: After setcontext, should never get here!!...\n");
}
