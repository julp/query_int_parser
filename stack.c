#include "stack.h"

#ifdef POSTGRESQL
extern int intarray_query_int_max_stack_size;
#endif /* POSTGRESQL */

typedef struct _StackElement {
    struct _StackElement *next;
    void *value;
} StackElement;

struct _Stack {
    DtorFunc df;
    size_t count;
    StackElement *head;
};

Stack *stack_new(DtorFunc df)
{
    Stack *this;

    this = mem_new(*this);
    this->df = df;
    this->count = 0;
    this->head = NULL;

    return this;
}

bool stack_push(Stack *this, void *value)
{
    StackElement *e;

#ifdef POSTGRESQL
    if (this->count >= intarray_query_int_max_stack_size) {
        return FALSE;
    } else
#endif /* POSTGRESQL */
    {
        e = mem_new(*e);
        e->next = this->head;
        e->value = value;
        this->head = e;
        ++this->count;

        return TRUE;
    }
}

// NOTE: caller should assume first that stack is not empty
void *stack_pop(Stack *this)
{
    void *value;
    StackElement *h;

    assert(NULL != this->head);

    h = this->head;
    value = this->head->value;
    this->head = this->head->next;
    --this->count;
    free(h);

    return value;
}

// NOTE: caller should assume first that stack is not empty
void *stack_top(Stack *this)
{
    assert(NULL != this->head);

    return this->head->value;
}

int stack_empty(Stack *this)
{
    return NULL == this->head;
}

void stack_destroy(Stack *this)
{
    StackElement *current, *next;

    current = this->head;
    while (NULL != current) {
        next = current->next;
        if (NULL != this->df) {
            this->df(current->value);
        }
        free(current);
        current = next;
    }
    free(this);
}
