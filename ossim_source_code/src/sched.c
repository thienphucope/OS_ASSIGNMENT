
#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;


#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;
	for (i = 0; i < MAX_PRIO; i ++)
		mlq_ready_queue[i].size = 0;
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
#ifdef MLQ_SCHED
    int o;
    for (o = 0; o < MAX_PRIO; o++) {
        mlq_ready_queue[o].size = 0;
        mlq_ready_queue[o].slot = 0; // Khởi tạo slot về 0
    }
#endif
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {
    struct pcb_t * proc = NULL;
    pthread_mutex_lock(&queue_lock);
    unsigned long prio;
    for (prio = 0; prio < MAX_PRIO; prio++) {
        // printf("Checking priority %lu: slot = %u\n", prio, mlq_ready_queue[prio].slot);
        if (mlq_ready_queue[prio].slot == MAX_PRIO - prio) {
            // printf("Priority %lu has reached its slot limit.\n", prio);
            continue;
        }
        if (!empty(&mlq_ready_queue[prio])) {
            proc = dequeue(&mlq_ready_queue[prio]);
            // printf("Dequeued process PID: %d from priority %lu\n", proc->pid, prio);
            mlq_ready_queue[prio].slot++; 
            break;
        }
    }
    if (mlq_ready_queue[MAX_PRIO-1].slot == 1) {
        // printf("Resetting all slots.\n");
        for (prio = 0; prio < MAX_PRIO; prio++)
            mlq_ready_queue[prio].slot = 0;
    }
    pthread_mutex_unlock(&queue_lock);
    return proc;	
}

void put_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);	
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;
	/*TODO: get a process from [ready_queue].
	 * Remember to use lock to protect the queue.
	 * */
	pthread_mutex_lock(&queue_lock);

    if (!empty(&ready_queue))
    {
        //int i = 0;
        while (!empty(&run_queue))
        {
            //i++;
            enqueue(&ready_queue, dequeue(&run_queue));
        }
        //printf("get     %d\n",i );
    }
    proc = dequeue(&ready_queue);
    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);	
}
#endif