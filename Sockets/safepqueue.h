#ifndef SAFEPQUEUE_H
#define SAFEPQUEUE_H


#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define MAX_QSIZE 1000
#define INF 999999

#define QFULL -2
#define QEMPTY -3
#define SUCCESS 0
#define FAILURE -1

typedef struct w{
    char *httpreq;
    char *path;
    int delay;
    int priority;
    int fd;  // client fd
    int wid; // work id
} work_t;

typedef work_t qitem_t;

// typedef struct {
//     int val;
//     int priority;
// } qitem_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t has_item;
    qitem_t **arr;
    int size;
    int max_size;
} pqueue_t;

// pqueue_t *create_queue();
pqueue_t *create_queue(int max_queue_size);
qitem_t *peek_at_top(pqueue_t *queue);
qitem_t *get_work(pqueue_t *queue);
qitem_t *get_work_nonblocking(pqueue_t *queue, int *qsize);
int add_work(pqueue_t *queue, qitem_t *item, int *qsize);
void print_queue(pqueue_t *queue);
void delete_queue(pqueue_t *queue);

#endif
