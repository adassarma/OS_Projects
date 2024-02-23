#include "safepqueue.h"
#include <stdio.h>
#include <stdlib.h>

int wid = 0;

// *************************************************
// -------------- helper functions ----------------
// *************************************************

void swap(qitem_t **a, qitem_t **b) {
    qitem_t *t;
    t = *a;
    *a = *b;
    *b = t;
}

int get_right_child(int index) {
    if ((((2 * index) + 1) < MAX_QSIZE) && (index >= 1))
        return (2 * index) + 1;
    return -1;
}

int get_left_child(int index) {
    if (((2 * index) < MAX_QSIZE) && (index >= 1))
        return 2 * index;
    return -1;
}

int get_parent(int index) {
    if ((index > 1) && (index < MAX_QSIZE)) {
        return index / 2;
    }
    return -1;
}

void max_heapify(pqueue_t *queue, int index) {
    int left_child_index = get_left_child(index);
    int right_child_index = get_right_child(index);

    // finding largest among index, left child and right child
    int largest = index;

    if ((left_child_index <= queue->size) && (left_child_index > 0)) {
        if (queue->arr[left_child_index]->priority > queue->arr[largest]->priority) {
            largest = left_child_index;
        }
    }

    if ((right_child_index <= queue->size && (right_child_index > 0))) {
        if (queue->arr[right_child_index]->priority > queue->arr[largest]->priority) {
            largest = right_child_index;
        }
    }

    // largest is not the node, node is not a heap
    if (largest != index) {
        swap(&queue->arr[index], &queue->arr[largest]);
        max_heapify(queue, largest);
    }
}

void build_max_heap(pqueue_t *queue) {
    int i;
    for (i = queue->size / 2; i >= 1; i--) {
        max_heapify(queue, i);
    }
}

qitem_t *peek_at_top(pqueue_t *queue) {
    return queue->arr[1];
}

qitem_t *extract_max(pqueue_t *queue) {
    qitem_t *maxm = queue->arr[1];
    queue->arr[1] = queue->arr[queue->size];
    queue->size--;
    max_heapify(queue, 1);
    return maxm;
}

void increase_key(pqueue_t *queue, int index, qitem_t *key) {
    queue->arr[index] = key;
    while ((index > 1) && (queue->arr[get_parent(index)]->priority < queue->arr[index]->priority)) {
        swap(&queue->arr[index], &queue->arr[get_parent(index)]);
        index = get_parent(index);
    }
}

void decrease_key(pqueue_t *queue, int index, qitem_t *key) {
    queue->arr[index] = key;
    max_heapify(queue, index);
}

void insert(pqueue_t *queue, qitem_t *key) {
    queue->size++;
    queue->arr[queue->size] = malloc(sizeof(qitem_t));
    queue->arr[queue->size]->priority = -1 * INF;
    increase_key(queue, queue->size, key);
}

// *************************************************
// ------------ helper functions end ---------------
// *************************************************

/**
 * assume this will be called in a single threaded environment
 */
pqueue_t *create_queue(int max_queue_size) {
    pqueue_t *queue = (pqueue_t *)malloc(sizeof(pqueue_t));
    // queue starts at index 1
    queue->arr = (qitem_t **)malloc((max_queue_size + 1) * sizeof(qitem_t *));
    queue->size = 0;
    queue->max_size = max_queue_size;
    wid = 0;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->has_item, NULL);
    return queue;
}

void delete_queue(pqueue_t *queue) {
    free(queue->arr);
    free(queue);
}

qitem_t *get_work(pqueue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    if (queue->size < 0) {
        printf("\tError: queue size negative\n");
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }
    while (queue->size == 0) {
        // printf("\tt%d is waiting for jobs to come\n", tid);
        pthread_cond_wait(&queue->has_item, &queue->lock);
    }
    qitem_t *item = extract_max(queue);
    pthread_mutex_unlock(&queue->lock);
    return item;
}

qitem_t *get_work_nonblocking(pqueue_t *queue, int *qsize) {
    pthread_mutex_lock(&queue->lock);
    if (queue->size <= 0) {
        *qsize = queue->size;
        printf("\tError: queue size %d\n", queue->size);
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }
    qitem_t *item = extract_max(queue);
    *qsize = queue->size;
    pthread_mutex_unlock(&queue->lock);
    return item;
}

int add_work(pqueue_t *queue, qitem_t *item, int *qsize) {
    pthread_mutex_lock(&queue->lock);
    if (queue->size == queue->max_size) {
        *qsize = queue->size;
        pthread_mutex_unlock(&queue->lock);
        return QFULL;
    }
    item->wid = wid++;
    insert(queue, item);
    *qsize = queue->size;
    pthread_cond_signal(&queue->has_item);
    // printf("Signaled\n");
    pthread_mutex_unlock(&queue->lock);
    return SUCCESS;
}

char *repr_item(qitem_t *item) {
    char *str = malloc(100 * sizeof(char));
    sprintf(str, "priority:%d, wid:%d, fd:%d, delay:%d", item->priority,
            item->wid, item->fd, item->delay);
    return str;
}

void print_queue(pqueue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    printf("\n\tWork Queue\n");
    int i;
    for (i = 1; i <= queue->size; i++) {
        printf("\t%s\n", repr_item(queue->arr[i]));
    }
    printf("\t----\n");
    pthread_mutex_unlock(&queue->lock);
}
