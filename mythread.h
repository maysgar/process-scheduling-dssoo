#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "interrupt.h"

#define N 10 /*maximum number of threads at the same time*/
#define FREE 0
#define INIT 1
#define WAITING 2
#define IDLE 3

#define STACKSIZE 10000
#define QUANTUM_TICKS 2 /*RR time slot*/

#define LOW_PRIORITY 0
#define HIGH_PRIORITY 1
#define SYSTEM 2

#define PRINT 1 //If it is 1 the messages are printed

/* Structure containing thread state  */
typedef struct tcb{
  int state; /* the state of the current block: FREE or INIT */
  int tid; /* thread id*/
  int priority; /* thread priority*/
  int ticks; /*execution time*/
  void (*function)(int);  /* the code of the thread */
  ucontext_t run_env; /* Context of the running environment*/
}TCB;

int mythread_create (void (*fun_addr)(), int priority); /* Creates a new thread with one argument */
void mythread_setpriority(int priority); /* Sets the thread priority */
int mythread_getpriority(); /* Returns the priority of calling thread*/
void mythread_exit(); /* Frees the thread structure and exits the thread */
int mythread_gettid(); /* Returns the thread id */
int read_network(); /* */

int tick_minus();/*It substract 1 tick and it checks if there is no more ticks, returning 0*/
void mythread_next();/*It changes the execution to the next thread*/
void activator_RR(TCB* actual, TCB* next);/*Activator with swapcontext*/
void activator_FIFO(TCB* next);/*Activator with setcontext*/
int getTicks();/*It returns the number of remaining ticks (only for debugging)*/
