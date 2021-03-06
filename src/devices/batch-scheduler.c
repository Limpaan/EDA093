/* Tests cetegorical mutual exclusion with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/random.h" //generate random numbers
#include "devices/timer.h"

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1

#define OPEN 2	//If the queue for the bus is empty, either direction can claim it

/*
 *	initialize task with direction and priority
 *	call o
 * */
typedef struct {
	int direction;
	int priority;
} task_t;

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive);

void senderTask(void *);
void receiverTask(void *);
void senderPriorityTask(void *);
void receiverPriorityTask(void *);


void oneTask(task_t task);/*Task requires to use the bus and executes methods below*/
	void getSlot(task_t task); /* task tries to use slot on the bus */
	void transferData(task_t task); /* task processes data on the bus either sending or receiving based on the direction*/
	void leaveSlot(task_t task); /* task release the slot */

struct lock mutex;
struct condition highSen;
struct condition lowSen;
struct condition highRec;
struct condition lowRec;
int direction = OPEN;
int waitingHighSender = 0;
int waitingHighReceiver = 0;
int openSlots = BUS_CAPACITY;

/* initializes semaphores */ 
void init_bus(void){ 

    random_init((unsigned int)123456789); 
    
	 lock_init(&mutex);
	 cond_init(&highSen);
	 cond_init(&lowSen);
	 cond_init(&highRec);
	 cond_init(&lowRec);
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive)
{
	unsigned int i = 0;
	//Creates a thread for every task
	for(i = 0; i<num_tasks_send; i++) {
		thread_create("SenderTask", 1, senderTask, NULL);
	}
	for(i = 0; i<num_task_receive; i++) {
		thread_create("ReceiverTask", 1, receiverTask, NULL);
	}
	for(i = 0; i<num_priority_send; i++) {
		thread_create("SenderPriorityTask", 63, senderPriorityTask, NULL);
	}
	for(i = 0; i<num_priority_receive; i++) {
		thread_create("ReceiverPriorityTask", 63, receiverPriorityTask, NULL);
	}
}

/* Normal task,  sending data to the accelerator */
void senderTask(void *aux UNUSED){
        task_t task = {SENDER, NORMAL};
        oneTask(task);
}

/* High priority task, sending data to the accelerator */
void senderPriorityTask(void *aux UNUSED){
        task_t task = {SENDER, HIGH};
        oneTask(task);
}

/* Normal task, reading data from the accelerator */
void receiverTask(void *aux UNUSED){
        task_t task = {RECEIVER, NORMAL};
        oneTask(task);
}

/* High priority task, reading data from the accelerator */
void receiverPriorityTask(void *aux UNUSED){
        task_t task = {RECEIVER, HIGH};
        oneTask(task);
}

/* abstract task execution*/
void oneTask(task_t task) {
  getSlot(task);
  transferData(task);
  leaveSlot(task);
}


/* task tries to get slot on the bus subsystem */
void getSlot(task_t task) 
{
	 lock_acquire(&mutex);
	 if(task.direction == SENDER) {
		if(task.priority == HIGH) {
			waitingHighSender++;
			while(openSlots <= 0 || direction == RECEIVER) {	//If you are a sender with high priority, only wait for full bus or wrong direction
				cond_wait(&highSen, &mutex);
			}
			waitingHighSender--;
			direction = task.direction;
			openSlots--;
		} else {
			while(openSlots <= 0 || direction == RECEIVER || waitingHighSender > 0 || waitingHighReceiver > 0) {	//If you are a sender with low priority, wait for full bus, wrong direction or any higher priority tasks.
				cond_wait(&lowSen, &mutex);
			}
			direction = task.direction;
			openSlots--;
		}
	 } else {
		if(task.priority == HIGH) {
			waitingHighReceiver++;
			while(openSlots <= 0 || direction == SENDER) {	//See above
				cond_wait(&highRec, &mutex);
			}
			waitingHighReceiver--;
			direction = task.direction;
			openSlots--;
		} else {
			while(openSlots <= 0 || direction == SENDER || waitingHighReceiver > 0 || waitingHighSender > 0) {	//See above
				cond_wait(&lowRec, &mutex);
			}
			direction = task.direction;
			openSlots--;
		}
	 }
	ASSERT(openSlots >= 0);			//Check that the bus is not overfull.
	if(task.priority == NORMAL) {
		ASSERT(waitingHighReceiver == 0 && waitingHighSender == 0);		//Check that no low priority task goes before a high priority task.
	}
	 lock_release(&mutex);
}

/* task processes data on the bus send/receive */
void transferData(task_t task) 
{
	timer_msleep(100 + random_ulong() % 100);
}

/* task releases the slot */
void leaveSlot(task_t task) 
{
	lock_acquire(&mutex);
	openSlots++;
	ASSERT(openSlots <= BUS_CAPACITY);			//Check that the number of slots on the bus is correct
	ASSERT(direction == task.direction);		//Check that the direction has not changed since the task entered
	/*
	 * If the bus is empty, which can happen either if no tasks from this direction are waiting
	 * or if high priority tasks exist on the opposite end and no high priority tasks from this direction are waiting.
	 */
	if(openSlots >= BUS_CAPACITY) {
		direction = OPEN;
	}
	/*
	 * Wake all a thread for each category that might want to run now. The thread checks if it can run or not.
	 */
	if(direction == SENDER) {
		if(waitingHighSender > 0) {
			cond_signal(&highSen, &mutex);
		} else {
			cond_signal(&lowSen, &mutex);
		}
	} else if(direction == RECEIVER) {
		if(waitingHighReceiver > 0) {
			cond_signal(&highRec, &mutex);
		} else {
			cond_signal(&lowRec, &mutex);
		}
	} else {
		int i = 0;
		/*
		 * When the bus is empty, try to wake three threads. The threads themselves will check which can run.
	 	*/
		for(i = 0; i<BUS_CAPACITY; i++) {
			cond_signal(&highRec, &mutex);
			cond_signal(&highSen, &mutex);
			cond_signal(&lowSen, &mutex);
			cond_signal(&lowRec, &mutex);
		}
	}

	lock_release(&mutex);
}
