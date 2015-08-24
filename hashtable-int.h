#ifndef HASHTABLE_INT_H

# define HASHTABLE_INT_H

typedef struct _HashNode {
    ht_hash_t hash;
    ht_key_t key;
    void *data;
    struct _HashNode *nNext; /* n = node */
    struct _HashNode *nPrev;
    struct _HashNode *gNext; /* g = global */
    struct _HashNode *gPrev;
} HashNode;

struct _HashTable {
    HashNode **nodes;
    HashNode *gHead;
    HashNode *gTail;
    HashFunc hf;
    CmpFunc cf;
    DupFunc key_duper;
    DtorFunc key_dtor;
    DtorFunc value_dtor;
    size_t capacity;
    size_t count;
    ht_hash_t mask;
};

#endif /* !HASHTABLE_INT_H */
