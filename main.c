#include <stdio.h>
#include <stdlib.h>
#include "mythread.h"
#define PRINT 0 //If it is 1 the messages are printed


void fun1 (int global_index){
  read_network();
  while (1) {
    /* code */
  }
  for (int a = 0; a < QUANTUM_TICKS; ++a) /*it simulates the time clock signals*/
	{
    if(PRINT == 1) printf ("Thread %d with priority %d\t from fun1 and remaining ticks %i\n", mythread_gettid(), mythread_getpriority(), getTicks());
    if(tick_minus() == 0){ /*checking if the thread has finished its execution*/
      printf("Time expired\n");
      mythread_exit(); /*I finish the thread*/
      return;
    }
  }
  mythread_next(); /*take the next thread and store the current one in the queue*/
  return;
}

void fun2 (int global_index){
  read_network();
  for (int a=0; a<QUANTUM_TICKS; ++a) {
    if(PRINT == 1) printf("Thread %d with priority %d\t from fun2 and remaining ticks %i\n", mythread_gettid(), mythread_getpriority(), getTicks());
    if(tick_minus() == 0){//The time has expired
      printf("Time expired\n");
      mythread_exit();//I finish the thread
      return;
    }
  }
  mythread_next();
  return;
}

void fun3 (int global_index){
  read_network();
  for (int a=0; a<QUANTUM_TICKS; ++a) {
    if(PRINT == 1) printf("Thread %d with priority %d\t from fun3 and remaining ticks %i\n", mythread_gettid(), mythread_getpriority(), getTicks());
    if(tick_minus() == 0){//The time has expired
      printf("Time expired\n");
      mythread_exit();//I finish the thread
      return;
    }
  }
  mythread_next();
  return;
}


int main(int argc, char *argv[])
{
  mythread_setpriority(HIGH_PRIORITY);
  read_network();
  if((mythread_create(fun1,LOW_PRIORITY)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }
  read_network();
  if((mythread_create(fun2,LOW_PRIORITY)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }
  if((mythread_create(fun3,LOW_PRIORITY)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }/* only 3 threads for testing
  if((mythread_create(fun1,HIGH_PRIORITY)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }

  if((mythread_create(fun2,HIGH_PRIORITY)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }
*/
  mythread_exit();

  printf("This program should never come here\n");

  return 0;
} /****** End main() ******/
