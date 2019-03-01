#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;
  
  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  int state;
  size_t wakeTime;
  task_t waitingOn;
  bool blocked;
  int input;
} task_info_t;

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
task_info_t tasks[MAX_TASKS]; //< Information for every task

size_t run_time = 0;
size_t start_time = 0;
size_t end_time = 0;
size_t sleep_time = 0;

#define AVAILABLE 0
#define RUNNING 1
#define BLOCKED 2
#define SLEEPING 3
#define ENDED 4

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functions in this file.
 */
void scheduler_init() {
  tasks[0].state = RUNNING;
} // schedule_init

void rescheduler() {
  printf("rescheduler\n");
  task_t new_task;
  // Look for new task
  int i = current_task + 1;
  start_time = time_ms();
  while (true) {
    if (i >= num_tasks) {
      i = 0;
    }

    //printf("end time = %zu sleep time %zu start time %zu\n", end_time, tasks[current_task].wakeTime, start_time);
    // Check when sleeping task is done
    /*
    if (end_time - start_time == tasks[i].wakeTime) {
      printf("end time = %zu\n", end_time);
      tasks[i].state = AVAILABLE;
      tasks[i].wakeTime = 0;
    }
    */
    // Get the one with smallest wakeTime 
    task_info_t current = tasks[0];
    //task_info_t earliestTask = tasks[0];
    //int k = 0;
    //while (k < num_tasks) {
      //if(current.wakeTime < earliestTask.wakeTime) 
        //earliestTask = current;
      //k++;
      //current = tasks[k];
    //}

    sleep_time = tasks[current_task].wakeTime;
    sleep_ms(sleep_time);
    int t = 0;
    //current = tasks[0];
    while (t < num_tasks) {
      current.wakeTime = current.wakeTime - sleep_time; 
      printf("%zu\n", current.wakeTime);
      t++;
      current = tasks[t];
    }

    // Update time for everyone

    if (tasks[i].state == SLEEPING && tasks[i].wakeTime <= 0) {
      tasks[i].state = AVAILABLE;
      tasks[i].wakeTime = 0;
    }

    // Find the next available task
    if (tasks[i].state == AVAILABLE) {
      //printf("new task %d\n", i);
      new_task = i;
      break;
    } 
      i++;
  }


  tasks[new_task].state = RUNNING;
  task_t temp = current_task;
  current_task = new_task;
  //end_time = time_ms();
  swapcontext(&tasks[temp].context, &tasks[new_task].context);
} // rescheduler


/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // Change state of current_task
  //printf("I'm done %d\n", current_task);

  // Free task blocked because of the exited task
  for (int j = 0; j < num_tasks; j++) {
      if (tasks[j].waitingOn == current_task){
         tasks[j].waitingOn = -1;
         tasks[j].state = AVAILABLE;
      }
  }

  tasks[current_task].state = ENDED;
  /*
  for (int i = 0; i < num_tasks; i++) {
    printf("task %d state %d waiton %d exit\n", i, tasks[i].state, tasks[i].waitingOn);
  }
  */
  //end_time = time_ms();
  rescheduler();
  // Get the next task
} // task_exit

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;
  
  // Set the task handle to this index, since task_t is just an int
  *handle = index;
 
  // We're going to make two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second
  
  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);
  
  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;
  
  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);
  
  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);
  
  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;
  
  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;
  
  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
  tasks[index].state = AVAILABLE;
  tasks[index].waitingOn = -1;
} // task_create

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // TODO: Block this task until the specified task has exited.
  if (tasks[handle].state == ENDED) // If task has already run, don't wait anymore
    return;
  //printf("wait %d\n", handle);
  tasks[current_task].waitingOn = handle;
  tasks[current_task].state = BLOCKED;
  /*
  for (int i = 0; i < num_tasks; i++) {
    printf("      task %d state %d waiton %d\n", i, tasks[i].state, tasks[i].waitingOn);
  }
  */
   rescheduler();
} // task_wait

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 * 
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // TODO: Block this task until the requested time has elapsed.
  // Hint: Record the time the task should wake up instead of the time left for it to sleep. The bookkeeping is easier this way
  tasks[current_task].state = SLEEPING;
  tasks[current_task].wakeTime = ms;
  //sleep_ms(ms);
  start_time = time_ms();
  //printf("sleep time %zu start time %zu\n", tasks[current_task].wakeTime, start_time);

  rescheduler();
  
} // task_sleep

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was available.
  // Otherwise, getch() will returns the character code that was read.
  return ERR;
  
} // task_readchar
