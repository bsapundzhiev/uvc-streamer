#ifndef _CQUEUE_H
#define _CQUEUE_H

typedef struct {
    int front;
    int rear;
    int count;
    int max;
    void **ele;
} cqueue_t;

void init_queue(cqueue_t * q, int size);
void *queue_front(cqueue_t * q);
void queue_push(cqueue_t * q, void *item);
void * queue_pop(cqueue_t * q);

#endif