#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include "RRFN.h"
#include "interrupt.h"

#include "queue.h"

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue_high; /*The queue which will have all the threads with high priority*/
static struct queue *tqueue_low; /*The queue which will have all the threads with low priority*/

static struct queue *waiting_queue; /*The waiting queue*/

static TCB* running; /* Current running thread */
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;
static int count = 0; /*to know in what RR tick are we*/

/* Thread control block for the idle thread */
TCB idle;

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
	printf("*** THREAD %d READY\n", t_state[i].tid);
	//printf("\n\n\t\tQueue of HIGH priority after inserting\n");
	//queue_print(tqueue_high);
	//printf("\n\n\t\tQueue of LOW priority after inserting\n");
	//queue_print(tqueue_low);
	if(priority == HIGH_PRIORITY && running -> priority != HIGH_PRIORITY){ /*High priority thread*/
		TCB* next = scheduler(); /*get the next thread to be executed*/
		printf("READ %d PREEMPTED: SET CONTEXT OF %d\n", running -> tid, next -> tid);
		activator(next); /*I initialize the next process*/
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
	TCB* aux;
	if(PRINT == 1) printf ("Thread %d with priority %d\t calls to the read_network\n", mythread_gettid(), mythread_getpriority(0));
	printf("*** THREAD %d READ FROM NETWORK\n", mythread_gettid());
	//running -> state = WAITING;
	
	TCB* next = scheduler(); //falta el caso en el que solo quedan las mierdas de la waiting queue

	printf("*** SWAPCONTEXT FROM %i to %i\n", running-> tid, next -> tid);
	/* Thread leaves the CPU & in introduced to the waiting queue */
	disable_interrupt(); /*block the signals while using the queue*/
	enqueue(waiting_queue, running); /*enqueue*/
	enable_interrupt(); /*Unlock the signals*/
	memcpy(&aux, &running, sizeof(TCB *));
	running = next;
	current = running -> tid;
	if(swapcontext (&(aux->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/

	return 1;
}

/* Network interrupt  */
/*
Emulates a NIC receiving a packet each second. When a new packet arrives,
this handler should dequeue the first thread from the waiting queue and 
enqueue it in the ready queue corresponding to its priority. If there is 
no thread waiting, the packet should be discarded
*/
void network_interrupt(int sig)
{

	TCB* aux;
	if((aux = dequeue(waiting_queue)) == NULL){ /* dequeue first thread from the waiting queue */ //cambiar a isempty()
		printf("Waiting queue is empty, discard packet\n");
		return; /* discard the packet */
	}
	disable_interrupt(); /*block the signals while using the queue*/
	if(aux -> priority == HIGH_PRIORITY){
		enqueue(tqueue_high, aux); /* enqueue thread in the high priority ready queue */
	}
	if(aux -> priority == LOW_PRIORITY){
		enqueue(tqueue_low, aux); /* enqueue thread in the low priority ready queue */
	}
	enable_interrupt(); /*Unlock the signals*/
	printf("*** THREAD %d READY\n", aux -> tid);
	//TCB* next = scheduler(); /*get the next thread to be executed*/
	//activator(next); /*I initialize the next process*/
}

/* Free terminated thread and exits */
void mythread_exit() {
	int tid = mythread_gettid(); /* get the id of the current thread */
	t_state[tid].state = FREE;
	free(t_state[tid].run_env.uc_stack.ss_sp); /* free memory  HABRÁ UE CAMBIARLO */
	TCB* next = scheduler(); /* get the next thread to be executed */
	printf("THREAD %d FINISHED\n", tid, next -> tid);
	printf("THREAD %d FINISHED: SET CONTEXT OF %d\n", tid, next -> tid);
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

TCB* scheduler(){
	if( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 1) && (queue_empty(waiting_queue) == 1)){ /*check if there are more threads to execute*/
		printf("*** THREAD %d FINISHED\n", running -> tid);
		printf("FINISH\n");
		exit(1);
	}
	else if( (queue_empty(tqueue_low) == 1) && (queue_empty(tqueue_high) == 1) && (queue_empty(waiting_queue) == 0)){ /* the waiting queue is the only one left */
		printf("Everything empty but the WAITING QUEUE");
		return &idle; /* return idle thread */
	}
	else if( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1) /*the high priority is empty but not the low*/
					&& (running -> priority == HIGH_PRIORITY)){ /*no more high priority processes*/
		TCB * aux;
		disable_interrupt(); /*block the signals while using the queue*/
		aux = dequeue(tqueue_low); /*dequeue*/
		enable_interrupt(); /*Unlock the signals*/
		return aux;
	}
	else if((queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 0)){ /*change of queues*/ /* cambiar de baja prioridad a uno de alta que acaba de llegar*/
		TCB * aux;
		disable_interrupt(); /*block the signals while using the queue*/
		aux = dequeue(tqueue_high); /*dequeue*/
		enable_interrupt(); /*Unlock the signals*/
		return aux;
	}
	else if(running -> priority == LOW_PRIORITY){ /*RR change*/ /* cambiar uno de baja prioridad a otro de baja */
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
		count ++;
	}
	if( (running -> priority == LOW_PRIORITY)  &&  (count == QUANTUM_TICKS)) /*RR time slice consumed for low priority*/
	{
		count = 0; /*restore the count*/
		TCB* next = scheduler(); /*get the next thread to be executed*/
		activator(next); /*I initialize the next process*/
	}
	//no se si hace falta condicion para el IDLE thread
}

void activator(TCB* next){
	if( (queue_empty(tqueue_low) == 0) && (queue_empty(tqueue_high) == 1) /* the high priority queue is empty but not the low */
					 && (running -> priority == HIGH_PRIORITY || running -> priority == SYSTEM)){ /* no more high priority processes */
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
		return;
	}
	else if(running -> priority == SYSTEM){ /* thread idle is changed */
		TCB* aux;
		memcpy(&aux, &running, sizeof(TCB *));
		printf("*** THREAD READY: SET CONTEXT TO %d\n", next -> tid);
		running = next;
		current = running -> tid;
		setcontext (&(next->run_env));
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
