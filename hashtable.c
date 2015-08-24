#include <string.h>
#include <ctype.h>

#include "hashtable.h"
#include "hashtable-int.h"

#define HASHTABLE_MIN_SIZE 8

static inline uint32_t nearest_power(size_t requested, size_t minimal)
{
    if (requested > 0x80000000) {
        return UINT32_MAX;
    } else {
        int i = 1;
        requested = MAX(requested, minimal);
        while ((1U << i) < requested) {
            i++;
        }

        return (1U << i);
    }
}

ht_hash_t value_hash(ht_key_t k)
{
    return (ht_hash_t) k;
}

int uint32_cmp(ht_key_t a, ht_key_t b)
{
    return a - b;
}

int ascii_cmp_cs(ht_key_t k1, ht_key_t k2)
{
    const char *string1 = (const char *) k1;
    const char *string2 = (const char *) k2;

    if (NULL == string1 || NULL == string2) {
        return string1 == string2;
    }

    return strcmp(string1, string2);
}

ht_hash_t ascii_hash_cs(ht_key_t k)
{
    ht_hash_t h = 5381;
    const char *str = (const char *) k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) *str++;
    }

    return h;
}

int ascii_cmp_ci(ht_key_t k1, ht_key_t k2)
{
    const char *string1 = (const char *) k1;
    const char *string2 = (const char *) k2;

    if (NULL == string1 || NULL == string2) {
        return string1 == string2;
    }

    return strcasecmp(string1, string2);
}

ht_hash_t ascii_hash_ci(ht_key_t k)
{
    ht_hash_t h = 5381;
    const char *str = (const char *) k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) toupper((unsigned char) *str++);
    }

    return h;
}

HashTable *hashtable_sized_new(
    size_t capacity,
    HashFunc hf,
    CmpFunc cf,
    DupFunc key_duper,
    DtorFunc key_dtor,
    DtorFunc value_dtor
) {
    HashTable *this;

    // allow NULL for hf for "numeric" keys by using directly the hash (key member point to hash member)
#if 0
    assert(NULL != hf);
    assert(NULL != cf);
#endif

    this = mem_new(*this);
    this->count = 0;
    this->gHead = NULL;
    this->gTail = NULL;
    this->capacity = nearest_power(capacity, HASHTABLE_MIN_SIZE);
    this->mask = this->capacity - 1;
    this->hf = hf;
    this->cf = cf;
    this->key_duper = key_duper;
    this->key_dtor = key_dtor;
    this->value_dtor = value_dtor;
    this->nodes = mem_new_n(*this->nodes, this->capacity);
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));

    return this;
}

HashTable *hashtable_new(HashFunc hf, CmpFunc ef, DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_sized_new(HASHTABLE_MIN_SIZE, hf, ef, key_duper, key_dtor, value_dtor);
}

HashTable *hashtable_ascii_cs_new(DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_sized_new(HASHTABLE_MIN_SIZE, ascii_hash_cs, ascii_cmp_cs, key_duper, key_dtor, value_dtor);
}

static inline void hashtable_rehash(HashTable *this)
{
    HashNode *n;
    uint32_t index;

    if (this->count < 1) {
        return;
    }
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));
    n = this->gHead;
    while (NULL != n) {
        index = n->hash & this->mask;
        n->nNext = this->nodes[index];
        n->nPrev = NULL;
        if (NULL != n->nNext) {
            n->nNext->nPrev = n;
        }
        this->nodes[index] = n;
        n = n->gNext;
    }
}

static inline void hashtable_maybe_resize(HashTable *this)
{
    if (this->count < this->capacity) {
        return;
    }
    if ((this->capacity << 1) > 0) {
        this->nodes = mem_renew(this->nodes, *this->nodes, this->capacity << 1);
        this->capacity <<= 1;
        this->mask = this->capacity - 1;
        hashtable_rehash(this);
    }
}

ht_hash_t hashtable_hash(HashTable *this, ht_key_t key)
{
    assert(NULL != this);

    return this->hf(key);
}

size_t hashtable_size(HashTable *this)
{
    assert(NULL != this);

    return this->count;
}

static bool hashtable_put_real(HashTable *this, uint32_t flags, ht_hash_t h, ht_key_t key, void *value, void **oldvalue)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && 0 == this->cf(key, n->key)) {
            if (NULL != oldvalue) {
                assert(NULL != oldvalue);
                *oldvalue = n->data;
            }
            if (!HAS_FLAG(flags, HT_PUT_ON_DUP_KEY_PRESERVE)) {
                if (NULL != this->value_dtor/* && !HAS_FLAG(flags, HT_PUT_ON_DUP_KEY_NO_DTOR)*/) {
                    this->value_dtor(n->data);
                }
                n->data = value;
                return TRUE;
            }
            return FALSE;
        }
        n = n->nNext;
    }
    n = mem_new(*n);
    if (NULL == this->key_duper) {
        n->key = key;
    } else {
        n->key = (ht_key_t) this->key_duper((void *) key);
    }
    n->hash = h;
    n->data = value;
    // Bucket: prepend
    n->nNext = this->nodes[index];
    n->nPrev = NULL;
    if (NULL != n->nNext) {
        n->nNext->nPrev = n;
    }
    // Global
    {
        HashNode *c;

        for (c = this->gHead; NULL != c && /*h > c->hash*/this->cf(n->key, c->key) > 0; c = c->gNext)
            ;
        if (NULL == c) { // no inferior element => append
            n->gPrev = this->gTail;
            this->gTail = n;
            n->gNext = NULL;
            if (NULL != n->gPrev) {
                n->gPrev->gNext = n;
            }
            if (NULL == this->gHead) {
                this->gHead = n;
            }
        } else { // insert before c (c is superior)
            n->gNext = c;
            n->gPrev = c->gPrev;
            if (NULL != c->gPrev) {
                c->gPrev->gNext = n;
            } else {
                this->gHead = n;
            }
            c->gPrev = n;
        }
    }
    this->nodes[index] = n;
    ++this->count;
    hashtable_maybe_resize(this);

    return TRUE;
}

void hashtable_put(HashTable *this, ht_key_t key, void *value, void **oldvalue)
{
    hashtable_put_real(this, 0, NULL == this->hf ? key : this->hf(key), key, value, oldvalue);
}

bool hashtable_quick_put_ex(HashTable *this, uint32_t flags, ht_hash_t h, ht_key_t key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, h, key, value, oldvalue);
}

bool hashtable_put_ex(HashTable *this, uint32_t flags, ht_key_t key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, NULL == this->hf ? key : this->hf(key), key, value, oldvalue);
}

void hashtable_direct_put(HashTable *this, ht_hash_t h, void *value, void **oldvalue)
{
    hashtable_put_real(this, 0, h, (ht_key_t) h, value, oldvalue);
}

bool hashtable_direct_put_ex(HashTable *this, uint32_t flags, ht_hash_t h, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, h, (ht_key_t) h, value, oldvalue);
}

bool hashtable_quick_get(HashTable *this, ht_hash_t h, ht_key_t key, void **value)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);
    assert(NULL != value);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && 0 == this->cf(key, n->key)) {
            *value = n->data;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_get(HashTable *this, ht_key_t key, void **value)
{
    return hashtable_quick_get(this, NULL == this->hf ? key : this->hf(key), key, value);
}

bool hashtable_direct_get(HashTable *this, ht_hash_t h, void **value)
{
    return hashtable_quick_get(this, h, h, value);
}

bool hashtable_quick_contains(HashTable *this, ht_hash_t h, ht_key_t key)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && 0 == this->cf(key, n->key)) {
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_contains(HashTable *this, ht_key_t key)
{
    return hashtable_quick_contains(this, NULL == this->hf ? key : this->hf(key), key);
}

bool hashtable_direct_contains(HashTable *this, ht_hash_t h)
{
    return hashtable_quick_contains(this, h, (ht_key_t) h);
}

static bool hashtable_delete_real(HashTable *this, ht_hash_t h, ht_key_t key, bool call_dtor)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && 0 == this->cf(key, n->key)) {
            if (n == this->nodes[index]) {
                this->nodes[index] = n->nNext;
            } else {
                n->nPrev->nNext = n->nNext;
            }
            if (NULL != n->nNext) {
                n->nNext->nPrev = n->nPrev;
            }
            if (NULL != n->gPrev) {
                n->gPrev->gNext = n->gNext;
            } else {
                this->gHead = n->gNext;
            }
            if (NULL != n->gNext) {
                n->gNext->gPrev = n->gPrev;
            } else {
                this->gTail = n->gPrev;
            }
            if (call_dtor && NULL != this->value_dtor) {
                this->value_dtor(n->data);
            }
            if (call_dtor && NULL != this->key_dtor) {
                this->key_dtor((void *) n->key);
            }
            free(n);
            --this->count;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_quick_delete(HashTable *this, ht_hash_t h, ht_key_t key, bool call_dtor)
{
    return hashtable_delete_real(this, h, key, call_dtor);
}

bool hashtable_delete(HashTable *this, ht_key_t key, bool call_dtor)
{
    return hashtable_delete_real(this, NULL == this->hf ? key : this->hf(key), key, call_dtor);
}

bool hashtable_direct_delete(HashTable *this, ht_hash_t h, bool call_dtor)
{
    return hashtable_delete_real(this, h, (ht_key_t) h, call_dtor);
}

static void hashtable_clear_real(HashTable *this)
{
    HashNode *n, *tmp;

    n = this->gHead;
    this->count = 0;
    this->gHead = NULL;
    this->gTail = NULL;
    while (NULL != n) {
        tmp = n;
        n = n->gNext;
        if (NULL != this->value_dtor) {
            this->value_dtor(tmp->data);
        }
        if (NULL != this->key_dtor) {
            this->key_dtor((void *) tmp->key);
        }
        free(tmp);
    }
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));
}

void hashtable_clear(HashTable *this)
{
    assert(NULL != this);

    hashtable_clear_real(this);
}

void hashtable_destroy(HashTable *this)
{
    assert(NULL != this);

    hashtable_clear_real(this);
    free(this->nodes);
    free(this);
}

static HashNode *hashtable_delete_node(HashTable *this, HashNode *n)
{
    HashNode *ret;

    if (NULL != n->nPrev) {
        n->nPrev->nNext = n->nNext;
    } else {
        uint32_t index;

        index = n->hash & this->mask;
        this->nodes[index] = n->nNext;
    }
    if (NULL != n->nNext) {
        n->nNext->nPrev = n->nPrev;
    }
    if (NULL != n->gPrev) {
        n->gPrev->gNext = n->gNext;
    } else {
        this->gHead = n->gNext;
    }
    if (NULL != n->gNext) {
        n->gNext->gPrev = n->gPrev;
    } else {
        this->gTail = n->gPrev;
    }
    --this->count;
    if (NULL != this->value_dtor) {
        this->value_dtor(n->data);
    }
    if (NULL != this->key_dtor) {
        this->key_dtor((void *) n->key);
    }
    ret = n->gNext;
    free(n);

    return ret;
}

void hashtable_foreach(HashTable *this, ForeachFunc ff)
{
    int ret;
    HashNode *n;

    n = this->gHead;
    while (NULL != n) {
        ret = ff(n->key, n->data);
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, n);
        } else {
            n = n->gNext;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_reverse(HashTable *this, ForeachFunc ff)
{
    int ret;
    HashNode *n, *tmp;

    n = this->gTail;
    while (NULL != n) {
        ret = ff(n->key, n->data);
        tmp = n;
        n = n->gPrev;
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, tmp);
        } else {
            n = n->gPrev;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_with_arg(HashTable *this, ForeachFunc ff, void *arg)
{
    int ret;
    HashNode *n;

    n = this->gHead;
    while (NULL != n) {
        ret = ff(n->key, n->data, arg);
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, n);
        } else {
            n = n->gNext;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_reverse_with_arg(HashTable *this, ForeachFunc ff, void *arg)
{
    int ret;
    HashNode *n, *tmp;

    n = this->gTail;
    while (NULL != n) {
        ret = ff(n->key, n->data, arg);
        tmp = n;
        n = n->gPrev;
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, tmp);
        } else {
            n = n->gPrev;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

#ifdef DEBUG
# include <stdio.h>
void hashtable_print(HashTable *this)
{
    size_t i;
    HashNode *n;

    for (i = 0; i < this->capacity; i++) {
        n = this->nodes[i];
        printf("%zu/%zu:\n", i, this->capacity);
        while (NULL != n) {
            printf("    %" PRIuPTR " <==> %p (%" PRIuPTR ")\n", n->key, n->data, n->hash);
            n = n->nNext;
        }
    }
    printf("\n");
}
#endif /* DEBUG */
