#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include "RR.h"
#include "interrupt.h"

#include "queue.h"

static TCB t_state[N]; /* Array of state thread control blocks: the process allows a maximum of N threads */
static struct queue *tqueue; /*The queue which will have all the threads*/

static TCB* running; /* Current running thread */
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init = 0;
static int count = 0; /*to know in what RR tick are we*/
static int idCount = -1;

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

/*Initialize the queue of processes*/
void init_RR(){
	tqueue = queue_new(); //I initialize the queue
	TCB aux;

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

	aux.state = INIT;
	aux.priority = LOW_PRIORITY;
	aux.ticks = QUANTUM_TICKS;
	aux.state = FREE;
	aux.tid = 0;

	t_state[0].state = INIT;
	t_state[0].tid = 0;


	if(getcontext(&aux.run_env) == -1)
	{
		perror("*** ERROR: getcontext in init_thread_lib");
		exit(5);
	}

	for(int i=1; i<N; i++){
		t_state[i].state = FREE;
	}

	printf("%i\n", aux.tid);
	running = &aux;
	printf("%i\n", running -> tid);

	/* Initialize network and clock interrupts */
	init_network_interrupt();
	init_interrupt();
}

/*It blocks all the signals
 * Return 0 in success and -1 otherwise*/
int blockSignals(){
	sigfillset( &maskval_interrupt );	/* Fill mask maskval_interrupt */
	sigprocmask( SIG_BLOCK, &maskval_interrupt, &oldmask_interrupt );
	return 0;
}

/*It unlock the signals
 * Return 0 in success and -1 otherwise*/
int unlockSignals(){
	sigprocmask( SIG_SETMASK, &oldmask_interrupt, NULL );	/* Restore process signal mask set in oldmask_interrupt */
	return 0;
}

/* Create and initialize a new thread with body fun_addr and one integer argument
 * It returns the position in the array of the new thread*/
int mythread_create (void (*fun_addr)(),int priority)
{
	int i;

	if (!init) /*if the library is not initialized*/
	{
		init_RR(); /*init the queue of threads*/
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

	blockSignals(); /*block the signals while using the queue*/
	enqueue(tqueue, &t_state[i]); /*enqueue the thread in the queue of threads*/
	unlockSignals(); /*Unlock the signals*/
	makecontext(&t_state[i].run_env, fun_addr, 1); /*create the new context*/
	printf("\t\tQueue after inserting\n");
	queue_print(tqueue);
	return i;
} /****** End my_thread_create() ******/

/* Problemas con el puto puntero*/
int mythread_createRR (void (*fun_addr)(),int priority)
{
	//int i;
	TCB aux;

	if (!init) /*if the library is not initialized*/
	{
		//init_mythreadlib();
		init_RR(); /*init the queue of threads*/
		init=1;
	}
	/*initialize the context of the thread which is going to be created*/
	if(getcontext(&aux.run_env) == -1)
	{
		perror("*** ERROR: getcontext in my_thread_create");
		exit(-1);
	}

	/*initialize the fields of the thread*/
	aux.state = INIT;
	aux.priority = priority; /*given by input to this method*/
	aux.function = fun_addr; /*given by input to this method*/
	aux.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
	if(aux.run_env.uc_stack.ss_sp == NULL)
	{
		printf("*** ERROR: thread failed to get stack space\n");
		exit(-1);
	}
	aux.tid = idCount++;
	aux.run_env.uc_stack.ss_size = STACKSIZE;
	aux.run_env.uc_stack.ss_flags = 0;
	aux.ticks = 3; /*inventado por mi*/

	blockSignals(); /*block the signals while using the queue*/
	enqueue(tqueue, &aux); /*enqueue the thread in the queue of threads*/
	unlockSignals(); /*Unlock the signals*/
	makecontext(&aux.run_env, fun_addr, 1); /*create the new context*/
	printf("\t\tQueue after inserting\n");
	queue_print(tqueue);
	return idCount;
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

	free(t_state[tid].run_env.uc_stack.ss_sp); /*free memory  HABRÁ UE CAMBIARLO*/

	running = schedulerRR(); /*get the next thread to be executed*/
	activator_FIFO(running); /*perform the context switch*/
}

/* We change the thread to the next one */
void mythread_next() {
	TCB* next;
	TCB *aux;

	printf("*** THREAD %d NO MORE TIME (ticks remaining: %i)\n", mythread_gettid(), running -> ticks);

	next = schedulerRR(); /*get the next thread to be executed*/
	blockSignals(); /*block the signals while using the queue*/
	enqueue(tqueue, running); /*enqueue the thread in the queue of threads*/
	unlockSignals(); /*Unlock the signals*/
	memcpy(&aux, &running, sizeof(TCB *));
	running = next;
	activator_RR(aux, next); /*perform the context switch*/
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
	return running->tid;
}

/*It substract 1 tick and it checks if there is no more ticks, returning 0*/
int tick_minus(){
	running -> ticks--; /* store the remaining ticks*/
	if(running -> ticks < 1) /*check if the thread has finished its execution's time*/
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

/* RR scheduler
 * the new thread to be executed is returned*/
TCB* schedulerRR(){
	if(queue_empty(tqueue) == 1){ /*check if there are more threads to execute*/
		printf("FINISH\n");
		exit(1);
	}
	else{ /*return the next thread in the queue*/
		TCB * aux;
		blockSignals(); /*block the signals while using the queue*/
		aux = dequeue(tqueue); /*dequeue*/
		unlockSignals(); /*Unlock the signals*/
		return aux;
	}
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
}

void activator_RR(TCB* actual, TCB* next){
	printf("*** SWAPCONTEXT FROM %i to %i\n", actual-> tid, next -> tid);
	if(swapcontext (&(actual->run_env), &(next->run_env)) == -1) printf("Swap error"); /*switch the context to the next thread*/
}

/* Activator */
void activator_FIFO(TCB* next){
	printf("*** THREAD %i FINISHED: SET CONTEXT OF %i\n", running-> tid, next -> tid);
	setcontext (&(next->run_env));
	printf("mythread_free: After setcontext, should never get here!!...\n");
}
