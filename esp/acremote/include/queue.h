typedef struct qnode {
    void *data;
    struct qnode *next;
} qnode;

typedef struct queue {
    qnode *head;
    qnode *tail;

    void (*push) (struct queue *q, void *data);
    void * (*pop) (struct queue *q);
    uint8_t len;
} queue;

queue *queue_new();
void queue_free(queue *q);
