#include <stdio.h>
#include <stdlib.h>

#include "cqueue.h"

void init_queue(cqueue_t * q, int size)
{
    q->count = q->front =  0;
    q->rear  = -1;
    q->max = size;
    q->ele = (void **)calloc(size, sizeof(void*));
}

static int is_full(cqueue_t * q)
{
   return (q->count == q->max);
}

static int is_empty(cqueue_t  * q)
{
	return (q->count == 0);
}

void *queue_front(cqueue_t * q)
{
	if(is_empty(q)) {
		return NULL;
	}
	return q->ele[q->front];
}

void queue_push(cqueue_t * q, void *item)
{
    if( is_full(q) ) {
       /*printf("Queue Overflow\n");*/
       q->count--;
    }

    q->rear = (q->rear+1) % q->max;
    q->ele[q->rear] = item;
    q->count++;
}

void * queue_pop(cqueue_t * q)
{
	void *item;
    if(is_empty(q)){
        /*printf("Queue empty\n");*/
        return NULL;
    }

    item = q->ele[q->front];
    q->front = (q->front+1) % q->max;
    q->count--;
    return item;
}
/*
#define MAX 3
int main()
{
    int i,item=0;
    cqueue_t  q;
    int val[6];

   init_queue(&q, MAX);

   for(i=0; i  < 6; i++){
   		val[i] = i * 10;
		queue_push(&q, (void*)&val[i]);
   }

   for(i=0; i  < MAX; i++){
	printf("%d\n", *(int*)q.ele[i]);
   }
   return 0;
}

*/