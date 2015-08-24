#ifndef HASHTABLE_H

# define HASHTABLE_H

# include "common.h"

typedef struct _HashTable HashTable;

typedef uintptr_t ht_key_t; // key_t is defined for ftok
typedef uintptr_t ht_hash_t;
typedef ht_hash_t (*HashFunc)(ht_key_t);

typedef int (*CmpFunc)(ht_key_t, ht_key_t);
typedef bool (*EqualFunc)(ht_key_t, ht_key_t);

# define HT_FOREACH_ACCEPT   (1<<1)
# define HT_FOREACH_REJECT   (1<<2)
# define HT_FOREACH_CONTINUE (1<<3)
# define HT_FOREACH_DELETE   (1<<4)
# define HT_FOREACH_STOP     (1<<5)

# define HT_PUT_ON_DUP_KEY_PRESERVE (1<<1)
/*# define HT_PUT_ON_DUP_KEY_NO_DTOR  (1<<2)*/

ht_hash_t ascii_hash_ci(ht_key_t);
ht_hash_t ascii_hash_cs(ht_key_t);
HashTable *hashtable_ascii_cs_new(DupFunc, DtorFunc, DtorFunc);
void hashtable_clear(HashTable *);
bool hashtable_contains(HashTable *, ht_key_t);
bool hashtable_direct_contains(HashTable *, ht_hash_t);
bool hashtable_quick_contains(HashTable *, ht_hash_t, ht_key_t);
bool hashtable_delete(HashTable *, ht_key_t, bool);
bool hashtable_direct_delete(HashTable *, ht_hash_t, bool);
bool hashtable_quick_delete(HashTable *, ht_hash_t, ht_key_t, bool);
void hashtable_destroy(HashTable *);
void hashtable_foreach(HashTable *, ForeachFunc);
void hashtable_foreach_reverse(HashTable *, ForeachFunc);
void hashtable_foreach_reverse_with_arg(HashTable *, ForeachFunc, void *);
void hashtable_foreach_reverse_with_args(HashTable *, ForeachFunc, int, ...);
void hashtable_foreach_with_arg(HashTable *, ForeachFunc, void *);
bool hashtable_get(HashTable *, ht_key_t, void **);
bool hashtable_direct_get(HashTable *, ht_hash_t, void **);
bool hashtable_quick_get(HashTable *, ht_hash_t, ht_key_t, void **);
ht_hash_t hashtable_hash(HashTable *, ht_key_t);
int uint32_cmp(ht_key_t, ht_key_t);
int ascii_cmp_ci(ht_key_t, ht_key_t);
int ascii_cmp_cs(ht_key_t, ht_key_t);
HashTable *hashtable_new(HashFunc, CmpFunc, DupFunc, DtorFunc, DtorFunc);
HashTable *hashtable_sized_new(size_t, HashFunc, CmpFunc, DupFunc, DtorFunc, DtorFunc);
void hashtable_put(HashTable *, ht_key_t, void *, void **);
bool hashtable_put_ex(HashTable *, uint32_t, ht_key_t, void *, void **);
void hashtable_direct_put(HashTable *, ht_hash_t, void *, void **);
bool hashtable_direct_put_ex(HashTable *, uint32_t, ht_hash_t, void *, void **);
bool hashtable_quick_put_ex(HashTable *, uint32_t, ht_hash_t, ht_key_t, void *, void **);
size_t hashtable_size(HashTable *);
bool value_equal(ht_key_t, ht_key_t);
ht_hash_t value_hash(ht_key_t);

# ifdef DEBUG
void hashtable_print(HashTable *);
# endif /* DEBUG */

#endif /* !HASHTABLE_H */
