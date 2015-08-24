#ifndef STACK_H

# define STACK_H

# include "common.h"

typedef struct _Stack Stack;

Stack *stack_new(DtorFunc df);
bool stack_push(Stack *, void *);
void *stack_pop(Stack *);
void *stack_top(Stack *);
int stack_empty(Stack *);
void stack_destroy(Stack *);

#endif /* !STACK_H */
