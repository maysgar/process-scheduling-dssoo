#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include "interrupt.h"

#include "mythread.h"

#include "queue.h"

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue; /*The queue which will have all the threads*/

static TCB* running; /* Current running thread */

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;

static int current = 0; /* current thread executing */

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void network_interrupt(int sig);


/* Thread control block for the idle thread */
TCB idle;

void idle_function(){
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
		init_mythreadlib(); /*init the queue of threads*/
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
	t_state[i].ticks = QUANTUM_TICKS;

	disable_interrupt(); /* block the signals while using the queue */
	enqueue(tqueue, &t_state[i]); /* enqueue the thread in the queue of threads */
	enable_interrupt(); /* Unlock the signals */
	makecontext(&t_state[i].run_env, fun_addr, 1); /* create the new context */
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
	int tid = mythread_gettid(); /* get the id of the current thread */

	printf("*** THREAD %d FINISHED\n", tid);
	t_state[tid].state = FREE;
	free(t_state[tid].run_env.uc_stack.ss_sp); /* free memory */

	TCB* next = scheduler(); /* get the next thread to be executed */
	activator(next); /* perform the context switch */
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
	int tid = mythread_gettid();
	t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority() {
	int tid = mythread_gettid();
	return t_state[tid].priority;
}

/* Get the current thread id.  */
int mythread_gettid(){
	if (!init) {init_mythreadlib(); init=1;}
	return current;
}

/* RR scheduler
 * the new thread to be executed is returned */
TCB* scheduler(){
	if(queue_empty(tqueue) == 1){ /* check if there are more threads to execute */
		printf("FINISH\n"); /* no more processes to be executed */
		exit(1);
	}
	else{ /* return the next thread in the queue */
		TCB * aux;
		disable_interrupt(); /* block the signals while using the queue */
		aux = dequeue(tqueue); /* dequeue the next process to be executed*/
		enable_interrupt(); /* Unlock the signals */
		return aux; /* return the next process to be executed */
	}
}

/*Activator*/
void activator(TCB* next){
	if(running -> state == FREE){ /*The process has finished*/
		printf("*** THREAD %i FINISHED: SET CONTEXT OF %i\n", running-> tid, next -> tid);
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		
		printf("mythread_free: After setcontext, should never get here!!...\n");
		return;;
	}
	else{ /*The process has already processing time*/
		TCB* aux;
		printf("*** SWAPCONTEXT FROM %i to %i\n", running-> tid, next -> tid);
		disable_interrupt(); /*block the signals while using the queue*/
		enqueue(tqueue, running); /*enqueue*/
		enable_interrupt(); /*Unlock the signals*/
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		if(swapcontext (&(aux->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/
		return;
	}
}


/* Timer interrupt
 * It checks if the RR slice has been executed */
void timer_interrupt(int sig)
{
	running->ticks --;
	if(running->ticks == 0) /* RR time slice consumed */
	{
		running->ticks = QUANTUM_TICKS; /* restore the count */
		TCB* next = scheduler(); /* get the next thread to be executed */
		activator(next); /* I initialize the next process */
	}
}
