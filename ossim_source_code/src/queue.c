#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
    // Kiểm tra queue và process có hợp lệ không
    if (q == NULL || proc == NULL) return;

    // Kiểm tra queue đã đầy chưa
    if (q->size >= MAX_QUEUE_SIZE) {
        printf("Queue is full. Cannot enqueue more processes.\n");
        return;
    }

    // Thêm process vào cuối queue
    q->proc[q->size] = proc;
    q->size++;
}

struct pcb_t * dequeue(struct queue_t * q) {
    // Kiểm tra queue có rỗng không
    if (q == NULL || q->size == 0) return NULL;

    // Tìm process có priority cao nhất
    int highest_prio_index = 0;
    int i;
    for (i = 1; i < q->size; i++) {
        // So sánh priority, nếu priority mới cao hơn thì cập nhật
        #ifdef MLQ_SCHED
        if (q->proc[i]->prio > q->proc[highest_prio_index]->prio) {
            highest_prio_index = i;
        }
        #else
        // Nếu không sử dụng multi-level queue, lấy phần tử đầu tiên
        highest_prio_index = 0;
        #endif
    }

    // Lấy process có priority cao nhất
    struct pcb_t * proc = q->proc[highest_prio_index];

    // Di chuyển các phần tử còn lại để lấp lỗ trống
    for (i = highest_prio_index; i < q->size - 1; i++) {
        q->proc[i] = q->proc[i + 1];
    }

    // Giảm kích thước queue
    q->size--;

    return proc;
}
