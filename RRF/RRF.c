#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue_high; /*The queue which will have all the threads with high priority*/
static struct queue *tqueue_low; /*The queue which will have all the threads with low priority*/

static TCB* running; /* Current running thread */
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;

/* Thread control block for the idle thread */
TCB idle;

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void network_interrupt(int sig);

void idle_function(){
	while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
	int i;

	tqueue_low = queue_new(); //I initialize the queues
	tqueue_high = queue_new(); //I initialize the queues

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
	//printf("Initialize network and clock interrupt\n");
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
	t_state[i].ticks=QUANTUM_TICKS;

	disable_interrupt(); /*block the signals while using the queue*/
	if(priority == LOW_PRIORITY){ /*Low priority thread*/
		enqueue(tqueue_low, &t_state[i]); /*enqueue the thread in the queue of low priority threads*/
	}
	else if(priority == HIGH_PRIORITY){ /*High priority thread*/
		enqueue(tqueue_high, &t_state[i]); /*enqueue the thread in the queue of high priority threads*/
	}
	else{
		printf("SYSTEM priority thread (IT SHOULD NOT ARRIVE HERE)\n");
	}
	enable_interrupt(); /*Unlock the signals*/
	makecontext(&t_state[i].run_env, fun_addr, 1); /*create the new context*/
	if(priority == HIGH_PRIORITY && running -> priority != HIGH_PRIORITY){ /*High priority thread*/
		//running -> ticks += current; /* restore ticks from the low priority thread */ ??commit holaquetal
		TCB* next = scheduler(); /* get the next thread to be executed */
		printf("READ %d PREEMPTED: SET CONTEXT OF %d\n", running -> tid, next -> tid);
		activator(next); /*I initialize the next process*/
	}
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
	t_state[tid].state = FREE;
	free(t_state[tid].run_env.uc_stack.ss_sp); /*free memory  HABRÁ QUE CAMBIARLO*/
	TCB* next = scheduler(); /*get the next thread to be executed*/
	printf("THREAD %d FINISHED\n", tid);
	printf("THREAD %d FINISHED: SET CONTEXT OF %d\n", tid, next -> tid);
	//count = 0; ?? commit holaquetal
	//printf("MYTHREAD_EXIT: A thread finishes so we execute the next one %d with priority %d\n", next -> tid, next -> priority);
	activator(next); /*perform the context switch*/
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

TCB* scheduler(){
	if( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 1) ){ /*check if there are more threads to execute*/
		printf("*** THREAD %d FINISHED\n", running -> tid);
		printf("FINISH\n");
		exit(1);
	}
	else if( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1) /*the high priority 
	is empty but not the low*/
					&& (running -> priority == HIGH_PRIORITY)){ /*no more high priority processes*/ /* no hace falta ultima condicion???? */
		//printf("Finishing last HIGH PRIORITY thread, we now change to the LOW PRIORITY threads\n");
		TCB * aux;
		disable_interrupt(); /*block the signals while using the queue*/
		aux = dequeue(tqueue_low); /*dequeue*/
		enable_interrupt(); /*Unlock the signals*/
		return aux;
	}
	else if((queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 0)){
		//printf("The HIGH PRIORITY queue is not empty, so a HIGH PRIORITY thread is executed next\n");
		TCB * aux;
		disable_interrupt(); /*block the signals while using the queue*/
		aux = dequeue(tqueue_high); /*dequeue*/
		enable_interrupt(); /*Unlock the signals*/
		return aux;
	}
	else if(running -> priority == LOW_PRIORITY){ /*RR change*/
		//printf("Change LOW priority thread by another LOW\n");
		TCB * aux;
		disable_interrupt(); /*block the signals while using the queue*/
		aux = dequeue(tqueue_low); /*dequeue*/
		enable_interrupt(); /*Unlock the signals*/
		return aux;
	}
	return NULL;
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
	if(running -> priority == LOW_PRIORITY){ /*check if count or not RR slice*/
		running->ticks--;
	}
	if( (running -> priority == LOW_PRIORITY)  &&  (running->ticks == 0)) /*RR time slice consumed for low priority*/
	{
		running->ticks = QUANTUM_TICKS; /*restore the count*/
		//printf("LOW PRIORITY thread finished its RR ticks, we do a change\n");
		TCB* next = scheduler(); /*get the next thread to be executed*/
		activator(next); /*I initialize the next process*/
	}
}

void activator(TCB* next){
	if( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1) /*the high priority queue is empty but not the low*/
					 && (running -> priority == HIGH_PRIORITY || running -> priority == SYSTEM)){ /*no more high priority processes*/
		//printf("Activate LOW PRIORITY thread after HIGH\n");
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		setcontext(&(next->run_env));
		return;
	}
	else if((running -> priority == LOW_PRIORITY) && next -> priority == HIGH_PRIORITY){ /* llega un thread de ALTA mientras uno de BAJA se ejectuta*/
		//printf("Activate HIGH PRIORITY thread after a LOW\n");
		TCB* aux;
		disable_interrupt(); /*block the signals while using the queue*/
		enqueue(tqueue_low, running); /*enqueue*/
		enable_interrupt(); /*Unlock the signals*/
		memcpy(&aux, &running, sizeof(TCB *));
		printf("*** SWAPCONTEXT FROM %i to %i\n", running-> tid, next -> tid);
		running = next;
		current = running -> tid;
		if(swapcontext (&(aux->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/
		return;
	}
	else if((queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 0)){ /* both queues have content */
		//printf("Activate the next HIGH PRIORITY thread\n");
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		return;
	}
	else if(running -> state == FREE){
		//printf("Activate the next HIGH PRIORITY thread\n");
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		return;
	}
	else if(running -> priority == LOW_PRIORITY){/*RR change*/
		//printf("Activate LOW PRIORITY after LOW\n");
		TCB* aux;
		disable_interrupt(); /*block the signals while using the queue*/
		enqueue(tqueue_low, running); /*enqueue*/
		enable_interrupt(); /*Unlock the signals*/
		memcpy(&aux, &running, sizeof(TCB *));
		printf("*** SWAPCONTEXT FROM %i to %i\n", running-> tid, next -> tid);
		running = next;
		current = running -> tid;
		if(swapcontext (&(aux->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/
		return;
	}
}
