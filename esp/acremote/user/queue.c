/*
 *  Copyright (C) 2017 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

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
