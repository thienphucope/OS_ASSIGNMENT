
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
static int slot[MAX_PRIO];
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
    
    int prio;
    int all_slots_zero = 1;  // Giả định tất cả slot đều về 0

    // Kiểm tra trạng thái slot của tất cả các queue
    for (prio = 0; prio < MAX_PRIO; prio++) {
        if (slot[prio] > 0) {
            all_slots_zero = 0;  // Còn slot chưa về 0
            break;
        }
    }

    // Chỉ reset khi TẤT CẢ slot về 0
    if (all_slots_zero) {
        for (prio = 0; prio < MAX_PRIO; prio++) {
            slot[prio] = MAX_PRIO - prio;  // Reset slot ban đầu
        }
    }

    // Tìm queue có process để xử lý
    for (prio = 0; prio < MAX_PRIO; prio++) {
        // Chỉ xử lý khi queue không rỗng VÀ CÒN SLOT
        if (!empty(&mlq_ready_queue[prio]) && slot[prio] > 0) {
            proc = dequeue(&mlq_ready_queue[prio]);
            slot[prio]--;
            break;  // Chỉ break khi tìm được process
        }
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
    if (empty(&ready_queue)) {
        // Move all processes from the run queue to the ready queue
        while (!empty(&run_queue)) {
            proc = dequeue(&run_queue);
            enqueue(&ready_queue, proc);
        }
    }
    if (!empty(&ready_queue)) {
        proc = dequeue(&ready_queue);
    }
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


