#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"

typedef struct qnode {
    uint8_t *data;
    struct qnode *next;
} qnode;

typedef struct queue {
    qnode *head;
    qnode *tail;

    void (*push) (struct queue *q, void *data);
    void * (*pop) (struct queue *q);
    uint8_t len;
} queue;

void push (queue *q, void *data) {
    qnode *n = (qnode *) os_zalloc(sizeof(qnode));
    n->data = data;
    n->next = NULL;

    if (q->head == NULL)
        q->head = n;
    else
        q->tail->next = n;

    q->tail = n;
    q->len++;
}

void *pop (queue *q) {
    qnode *head = q->head;
    void *data = head->data;

    q->head = head->next;
    q->len--;

    os_free(head);

    return data;
}

queue *queue_new() {
    queue *q = (queue *) os_zalloc(sizeof(queue));
    q->len = 0;
    q->head = NULL;
    q->tail = NULL;
    q->push = push;
    q->pop = pop;

    return q;
}

void queue_free(queue *q) {
    while (q->len > 0)
        q->pop(q);

    os_free(q);
}
