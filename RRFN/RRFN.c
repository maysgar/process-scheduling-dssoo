#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include "mythread.h"
#include "interrupt.h"


#include "queue.h"

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue_high; /* The queue which will have all the threads with high priority */
static struct queue *tqueue_low; /* The queue which will have all the threads with low priority */

static struct queue *waiting_queue; /* The queue of waiting threads */

static TCB* running; /* Current running thread */
static int current = 0; /* ID of the running thread */

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
	waiting_queue = queue_new(); // Initialize waiting queue

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

	if (!init) /* if the library is not initialized */
	{
		init_mythreadlib(); /* init the queue of threads */
		init=1;
	}
	for (i=0; i<N; i++) /* search for a free position in the thread array */
	{
		if (t_state[i].state == FREE) break;
	}
	if (i == N) /* the last position is not a valid one */
	{
		return(-1);
	}

	/* initialize the context of the thread which is going to be created */
	if(getcontext(&t_state[i].run_env) == -1)
	{
		perror("*** ERROR: getcontext in my_thread_create");
		exit(-1);
	}

	/* initialize the fields of the thread */
	t_state[i].state = INIT;
	t_state[i].priority = priority; /* given by input to this method */
	t_state[i].function = fun_addr; /* given by input to this method */
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
	if(priority == LOW_PRIORITY){ /* Low priority thread */
		enqueue(tqueue_low, &t_state[i]); /* enqueue the running thread in the queue of LOW priority threads */
	}
	else if(priority == HIGH_PRIORITY){ /*High priority thread*/
		enqueue(tqueue_high, &t_state[i]); /*enqueue the running thread in the queue of HIGH priority threads*/
	}
	else{ /* System thread running */
		printf("SYSTEM priority thread (IT SHOULD NOT ARRIVE HERE)\n");
	}
	enable_interrupt(); /* Unlock the signals */
	makecontext(&t_state[i].run_env, fun_addr, 1); /* create the new context */
	/* check if a HIGH process has arrived and the running is of LOW priority */
	if(priority == HIGH_PRIORITY && running -> priority != HIGH_PRIORITY){
		TCB* next = scheduler(); /* get the next thread to be executed */
		printf("READ %d PREEMPTED: SET CONTEXT OF %d\n", running -> tid, next -> tid);
		activator(next); /* I initialize the next process */
	}
	return i;
} /****** End my_thread_create() ******/

/* Read network syscall */
/*
When the function read network is called by a given thread
it should leave the CPU and introduced in a waiting queue 
 */
int read_network()
{
    if(running -> tid == 0) return 0;
	TCB* aux;
	printf("*** THREAD %d READ FROM NETWORK\n", mythread_gettid());

	TCB* next = scheduler(); /* get the next process to be executed */

	printf("*** SWAPCONTEXT FROM %i to %i\n", running-> tid, next -> tid);
	/* Thread leaves the CPU & in introduced to the waiting queue */
	disable_interrupt(); /* block the signals while using the queue */
	enqueue(waiting_queue, running); /* enqueue the running thread into the waiting queue */
	enable_interrupt(); /* Unlock the signals */
	memcpy(&aux, &running, sizeof(TCB *));
	running = next; /* update the running process */
	current = running -> tid; /* update current */
	if(swapcontext (&(aux->run_env), &(next->run_env)) == -1) printf("Swap error"); /* switch the context to the next thread with running  */
	return 1;
}

/* Network interrupt */
/*
Emulates a NIC receiving a packet each second. When a new packet arrives,
this handler should dequeue the first thread from the waiting queue and 
enqueue it in the ready queue corresponding to its priority. If there is 
no thread waiting, the packet should be discarded
*/
void network_interrupt(int sig)
{
	TCB* aux;
	if((aux = dequeue(waiting_queue)) == NULL){ /* dequeue the first thread from the waiting queue */
		return; /* discard the packet */
	}
	disable_interrupt(); /* block the signals while using the queue */
	if(aux -> priority == HIGH_PRIORITY){ /* the next process is a HIGH priority */
		enqueue(tqueue_high, aux); /* enqueue thread in the HIGH priority ready queue */
	}
	if(aux -> priority == LOW_PRIORITY){ /* the next process is a LOW priority */
        enqueue(tqueue_low, aux); /* enqueue thread in the LOW priority ready queue */
	}
	enable_interrupt(); /* Unlock the signals */
	printf("*** THREAD %d READY\n", aux -> tid);
	/* If the IDLE thread is running we change it to the new thread in the ready queue */
	if(running -> priority == SYSTEM){
		activator(scheduler());
	}
}

/* Free terminated thread and exits */
void mythread_exit() {
	int tid = mythread_gettid(); /* get the id of the current thread */
	t_state[tid].state = FREE;
	free(t_state[tid].run_env.uc_stack.ss_sp); /* free memory */
	TCB* next = scheduler(); /* get the next thread to be executed */
	printf("*** THREAD %d FINISHED\n", tid);
	printf("*** THREAD %d FINISHED: SET CONTEXT OF %d\n", tid, next -> tid);
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

/* RRFN scheduler
 * the new thread to be executed is returned */
TCB* scheduler(){
	/* check if there are more threads to execute in any queue */
    if( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 1) && (queue_empty(waiting_queue) == 1)){
		printf("*** THREAD %d FINISHED\n", running -> tid);
		printf("FINISH\n");
		exit(1);
    }
	/* the waiting queue is the only one with processes */
	else if( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 1) && (queue_empty(waiting_queue) == 0)){
		printf("Everything empty but the WAITING QUEUE");
		return &idle; /* return idle thread */
	}
	/* the HIGH priority queue is empty and the low is not
	 * The running process is of HIGH priority */
	else if( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1)
					&& (running -> priority == HIGH_PRIORITY)){ /*no more high priority processes*/
		TCB * aux;
		disable_interrupt(); /* block the signals while using the queue */
		aux = dequeue(tqueue_low); /* dequeue the next low process */
		enable_interrupt(); /* Unlock the signals */
		return aux;
	}
	/* the LOW priority queue is empty but the high queue is not
	 * or both queues have processes */
    else if (( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 0)) ||
			((queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 0))	) {
        TCB * aux;
        disable_interrupt(); /* block the signals while using the queue */
        aux = dequeue(tqueue_high); /* dequeue the next high process */
        enable_interrupt(); /* Unlock the signals */
        return aux;
    }
	/* RR change from a LOW process to a LOW process */
	else if(running -> priority == LOW_PRIORITY){
		TCB * aux;
		disable_interrupt(); /* block the signals while using the queue */
		aux = dequeue(tqueue_low);  /* dequeue the next low process */
		enable_interrupt(); /* Unlock the signals */
		return aux;
	}
	return NULL;
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
	if(running -> priority == LOW_PRIORITY){ /* check if running is of LOW priority*/
		running->ticks --;
	}
	if( (running -> priority == LOW_PRIORITY)  &&  (running->ticks == 0)) /* RR time slice consumed for LOW priority */
	{
        running->ticks = QUANTUM_TICKS; /* restore the count */
		TCB* next = scheduler(); /* get the next process to be executed */
        activator(next); /* I initialize the next process */
    }
}

/*Activator*/
void activator(TCB* next){
	/*
	 - If the LOW priority queue has content and the HIGH priority is empty,
	 and the one running is HIGH or IDLE
	 OR
	 - If the current running thread is the IDLE
	 */
	if(( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1) && (running -> priority == HIGH_PRIORITY || running -> priority == SYSTEM)) ||
	(running -> priority == SYSTEM)){
        TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		if(running -> priority == SYSTEM){
			printf("*** THREAD READY: SET CONTEXT TO %d\n", next -> tid);
		}
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		return;
	}

	/*
	 - If the running thread is LOW priority && the next thread is HIGH priority
	 */
	else if((running -> priority == LOW_PRIORITY) && (next -> priority == HIGH_PRIORITY)){ /* llega un thread de ALTA mientras uno de BAJA se ejectuta*/
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
	/*
	 - If NONE of the queues are empty
	 OR
	 - If the state of the running thread is FREE (comes from thread_exit())
	 */
	else if(((queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 0)) || (running -> state == FREE)){ /* both queues have content */
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		return;
	}
	else if(running -> priority == LOW_PRIORITY){/*RR change*/
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
