#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef struct Queue_Node Queue_Node;
typedef struct Queue Queue;

struct Queue_Node{
        void *content;
        Queue_Node *next;
};

struct Queue{
        Queue_Node *head;
        Queue_Node *tail;
        int size;
};


Queue *Queue_init();

void Queue_destroy(Queue *queue);

unsigned int Queue_getSize(Queue* queue);

int Queue_isEmpty(Queue *queue);

void Queue_enqueue(Queue *queue, void *element);

void *Queue_dequeue(Queue *queue);

void *Queue_get(Queue *queue, int index);

Queue *Queue_merge(Queue *, Queue *);