#include <string.h>

#ifdef POSTGRESQL
# include "postgres.h"
# include "miscadmin.h"
// # include "utils/builtins.h"
# include "utils/varbit.h"
# include "utils/guc.h"
#else
# include <stdio.h>
#endif /* POSTGRESQL */

#include "common.h"
#include "stack.h"
#include "parsenum.h"
#include "hashtable.h"

#define I(x) (int)(x)

#ifdef POSTGRESQL
# ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
# endif /* PG_MODULE_MAGIC */

void _PG_init(void);
PG_FUNCTION_INFO_V1(compile_query_int);
Datum compile_query_int(PG_FUNCTION_ARGS);

static int intarray_query_int_max_symbols;
/*static */int intarray_query_int_max_stack_size;
#else
# define POP(a, b) \
    b
# define ereport(type, code_msg) \
    POP code_msg
# define errcode(code) \
    /* NOP */
# define errmsg(fmt, ...) \
    fprintf(stderr, fmt "\n", ## __VA_ARGS__)
#endif /* POSTGRESQL */

typedef struct _QINode QINode;

enum {
    ASSOC_NONE,
    ASSOC_LEFT,
    ASSOC_RIGHT
};

enum {
    NONE = 0,
    UNARY = 1,
    BINARY = 2
};

typedef enum {
#define NODE(constant, name, characters, parsecb, evalcb) \
    constant,
#define OPERATOR(constant, name, characters, parsecb, evalcb, associativity, precedence, arity) \
    constant,
#include "nodes.h"
#undef NODE
#undef OPERATOR
} QINodeType;

typedef struct {
    QINode *root;
    HashTable *symbols;
} ParseResult;

static int eval_or(QINode *, uint32_t i);
#ifdef WITH_EXTRA_XOR
static int eval_xor(QINode *, uint32_t i);
#endif /* WITH_EXTRA_XOR */
static int eval_not(QINode *, uint32_t i);
static int eval_and(QINode *, uint32_t i);
static int eval_int(QINode *, uint32_t i);
static bool parse_int_symbol(HashTable *, QINode *, const char **, const char * const);
static QINode *NEW_NODE(QINodeType, size_t);
static bool parse(const char *, const char * const, ParseResult *);
#ifndef NO_NEED_TO_FREE
static void free_tree_node(QINode *);
static void free_tree(QINode *);
#endif /* !NO_NEED_TO_FREE */
static int eval_tree(QINode *, uint32_t);
static void compile_start_states(void);
static char *allocate_buffer(void *, size_t);
static uint8_t *compute_hash(void *, ParseResult *, uint8_t *, uint8_t *);

struct QINodeImplementation {
    const char *characters;
    const char *name;
    int (*eval)(QINode *, uint32_t i);
    bool (*parse)(HashTable *, QINode *, const char **, const char * const);
    int associativity;
    int precedence;
    int arity; // UNARY or BINARY
} static available_nodes[] = {
#define NODE(constant, name, characters, parsecb, evalcb) \
    { characters, name, evalcb, parsecb, ASSOC_NONE, 0, NONE },
#define OPERATOR(constant, name, characters, parsecb, evalcb, associativity, precedence, arity) \
    { characters, name, evalcb, parsecb, associativity, precedence, arity },
#include "nodes.h"
#undef NODE
#undef OPERATOR
};

struct _QINode {
    QINodeType type;
    QINode *left;
    QINode *right;
    size_t offset;
    uint32_t *value;
};

static QINodeType assignments[256] = { 0 };

static int eval_or(QINode *self, uint32_t i)
{
    return available_nodes[self->left->type].eval(self->left, i) || available_nodes[self->right->type].eval(self->right, i);
}

#ifdef WITH_EXTRA_XOR
static int eval_xor(QINode *self, uint32_t i)
{
    return available_nodes[self->left->type].eval(self->left, i) ^ available_nodes[self->right->type].eval(self->right, i);
}
#endif /* WITH_EXTRA_XOR */

static int eval_and(QINode *self, uint32_t i)
{
    return available_nodes[self->left->type].eval(self->left, i) && available_nodes[self->right->type].eval(self->right, i);
}

static int eval_not(QINode *self, uint32_t i)
{
    return !available_nodes[self->left->type].eval(self->left, i);
}

static int eval_int(QINode *self, uint32_t i)
{
    return i & *self->value;
}

static QINode *NEW_NODE(QINodeType type, size_t offset) {
    QINode *n;

    n = mem_new(*n);
    n->type = type;
    n->value = NULL;
    n->offset = offset;
    n->left = n->right = NULL;

    return n;
}

static bool parse_int_symbol(HashTable *symbols, QINode *node, const char **p, const char * const end)
{
    char *endptr;
    uint32_t val;
    ParseNumError pne;

    if (PARSE_NUM_NO_ERR != (pne = strntouint32_t(*p, end, &endptr, &val)) && (PARSE_NUM_ERR_NON_DIGIT_FOUND != pne)) {
        ereport(
            ERROR,
            (
                errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid number at offset %" PRIszu, node->offset)
            )
        );
        return FALSE;
    }
    debug("SYMBOL : >%.*s< (%" PRIu32 ")", I(endptr - *p), *p, val);
    if (!hashtable_direct_get(symbols, (ht_hash_t) val, (void **) &node->value)) {
        node->value = mem_new(*node->value);
        hashtable_direct_put(symbols, (ht_hash_t) val, node->value, NULL);
    }
    *p = endptr;

    return TRUE;
}

#ifdef POSTGRESQL
# define STACK_OVERFLOW(stack) \
    do { \
        ereport( \
            ERROR, \
            ( \
                errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED), \
                errmsg("internal stack size " #stack " for parsing query_int exceeds the maximum allowed by 'intarray.query_int.max_stack_size' GUC (%d)", intarray_query_int_max_stack_size) \
            ) \
        ); \
        goto end; \
    } while (0);
#else
# define STACK_OVERFLOW(stack) /* NOP */
#endif /* POSTGRESQL */

#ifdef DEBUG
# define PARSER_LINE_D \
    const unsigned int __parser_line
# define PARSER_LINE_DC \
    PARSER_LINE_D,
# define PARSER_LINE_C \
    __LINE__
# define PARSER_LINE_CC \
    PARSER_LINE_C,
#else
# define PARSER_LINE_D  /* NOP */
# define PARSER_LINE_DC /* NOP */
# define PARSER_LINE_C  /* NOP */
# define PARSER_LINE_CC /* NOP */
#endif

static bool handle_operator(PARSER_LINE_DC Stack *output, Stack *operators, QINode *node, QINode *op)
{
    stack_pop(operators);
    debug("POP(operators) %s (%d)", available_nodes[op->type].name, __parser_line);
    if (stack_empty(output)) {
        ereport(
            ERROR,
            (
                errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid expression, lvalue expected before offset %" PRIszu, op->offset)
            )
        );
#ifndef NO_NEED_TO_FREE
        if (NULL != node) {
            free(node);
        }
        free_tree_node(op);
#endif /* !NO_NEED_TO_FREE */
        goto end;
    }
    op->left = stack_pop(output);
    debug("POP(output) %s (%d)", available_nodes[op->left->type].name, __parser_line);
    if (available_nodes[op->type].arity > UNARY) {
        if (stack_empty(output)) {
            ereport(
                ERROR,
                (
                    errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid expression, rvalue expected after offset %" PRIszu, op->offset)
                )
            );
#ifndef NO_NEED_TO_FREE
            if (NULL != node) {
                free(node);
            }
            free_tree_node(op);
#endif /* !NO_NEED_TO_FREE */
            goto end;
        }
        op->right = stack_pop(output);
        debug("POP(output) %s (%d)", available_nodes[op->right->type].name, __parser_line);
    }
    if (!stack_push(output, op)) {
        STACK_OVERFLOW(output);
    }
    debug("PUSH(output) %s (%d)", available_nodes[op->type].name, __parser_line);

    return TRUE;
end:
    return FALSE;
}

static bool parse(const char *expr, const char * const end, ParseResult *result)
{
    QINode *node;
    const char *p;
    Stack *output, *operators;

    result->root = NULL;
#ifndef NO_NEED_TO_FREE
    output = stack_new((DtorFunc) free_tree_node);
    operators = stack_new((DtorFunc) free_tree_node);
#else
    output = stack_new(NULL);
    operators = stack_new(NULL);
#endif /* !NO_NEED_TO_FREE */
    result->symbols = hashtable_new(NULL, uint32_cmp, NULL, NULL, free_func_name);
    debug("EXPR is >%.*s<", I(end - expr), expr);
    for (p = expr; p < end; /* NOP */) {
        QINodeType type;

        type = assignments[(unsigned char) *p];
        if (!type) {
            ereport(
                ERROR,
                (
                    errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid character '%c' at offset %ld\n\t%.*s\n\t%*c", *p, p - expr, I(end - expr), expr, I(p - expr + 1), '^')
                )
            );
            goto end;
        } else {
            struct QINodeImplementation imp;

            node = NEW_NODE(type, p - expr);
            imp = available_nodes[type];
            if (NULL == imp.parse) {
                ++p;
                if (type == T_LPAREN) {
                    debug("(");
                    if (!stack_push(operators, node)) {
                        STACK_OVERFLOW(operators);
                    }
                    debug("PUSH(operators) %s (%d)", available_nodes[node->type].name, __LINE__);
                } else if (type == T_RPAREN) {
                    QINode *op;

                    debug(")");
                    op = NULL;
                    while (
                        !stack_empty(operators)
                        && (op = stack_top(operators))
                        && T_LPAREN != op->type
                    ) {
                        /**
                         * Examples of possible errors:
                         * - missing lvalue: '|)'
                         * - missing rvalue: '3&)'
                         **/
                        if (!handle_operator(PARSER_LINE_CC output, operators, node, op)) {
                            goto end;
                        }
                    }
                    if (stack_empty(operators)) { /* '(1))&3+' */
                        ereport(
                            ERROR,
                            (
                                errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                                errmsg("invalid expression, parentheses mismatch for ')' at offset %" PRIszu, node->offset)
                            )
                        );
#ifndef NO_NEED_TO_FREE
                        free(node);
                        if (NULL != op) {
                            free_tree_node(op);
                        }
#endif /* !NO_NEED_TO_FREE */
                        goto end;
                    }
                    /* op = */stack_pop(operators); /* '(' */
                    debug("POP(output) %s (%d)", available_nodes[op->type].name, __LINE__);
#ifndef NO_NEED_TO_FREE
                    free(node); /* ')' */
                    free(op);
#endif /* !NO_NEED_TO_FREE */
                }  else if (NULL != imp.eval) {
                    QINode *op;

                    debug("OPERATOR : %c", p[-1]);
                    if (imp.arity > UNARY) {
                        while (
                            !stack_empty(operators)
                            && (op = stack_top(operators))
                            && (
                                (ASSOC_LEFT == imp.associativity && imp.precedence <= available_nodes[op->type].precedence)
                                ||
                                (imp.precedence < available_nodes[op->type].precedence)
                            )
                        ) {
                            /**
                             * Examples of possible errors:
                             * - missing lvalue: '&&'
                             * - missing rvalue: '1&&'
                             **/
                            if (!handle_operator(PARSER_LINE_CC output, operators, node, op)) {
                                goto end;
                            }
                        }
                    }
                    if (!stack_push(operators, node)) {
                        STACK_OVERFLOW(operators);
                    }
                    debug("PUSH(operators) %s (%d)", available_nodes[node->type].name, __LINE__);
                }
#ifndef NO_NEED_TO_FREE
                else {
                    free(node); /* <ignorable> */
                }
#endif /* !NO_NEED_TO_FREE */
            } else {
                if (!imp.parse(result->symbols, node, &p, end)) { /* 99999999999999999999999999999 */
                    fprintf(stderr, "invalid expression, failed to parse ...\n");
#ifndef NO_NEED_TO_FREE
                    free(node);
#endif /* !NO_NEED_TO_FREE */
                    goto end;
                }
                if (!stack_push(output, node)) {
                    STACK_OVERFLOW(output);
                }
                debug("PUSH(output) %s (%d)", available_nodes[node->type].name, __LINE__);
            }
        }
    }
    while (!stack_empty(operators)) {
        QINode *op;

        op = stack_top(operators);
        if (T_LPAREN == op->type) { /* '1|(' */
            ereport(
                ERROR,
                (
                    errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid expression, parentheses mismatch for '(' at offset %" PRIszu, op->offset)
                )
            );
            goto end;
        } else {
            /**
             * Examples of possible errors:
             * - missing lvalue: '1|(&3)'
             * - missing rvalue: '(1|3)&(2)|'
             **/
            if (!handle_operator(PARSER_LINE_CC output, operators, NULL, op)) {
                goto end;
            }
        }
    }
    if (!stack_empty(output)) { /* '3 4' */
        QINode *n;

        n = stack_pop(output);
        debug("POP(output) %s (%d)", available_nodes[n->type].name, __LINE__);
        if (stack_empty(output)) {
            result->root = n;
        } else {
            ereport(
                ERROR,
                (
                    errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid expression, remaining element found at offset %" PRIszu, ((QINode *) stack_top(output))->offset)
                )
            );
#ifndef NO_NEED_TO_FREE
            free_tree_node(n);
#endif /* !NO_NEED_TO_FREE */
            goto end; /* not really useful */
        }
    } else { /* '' like '()' */
        ereport(
            ERROR,
            (
                errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid expression, empty expression found")
            )
        );
    }

end:
#ifndef NO_NEED_TO_FREE
    stack_destroy(output);
    stack_destroy(operators);
#endif /* !NO_NEED_TO_FREE */

    return NULL != result->root;
}

#ifndef NO_NEED_TO_FREE
static void free_tree_node(QINode *n)
{
    if (NULL != n->left) {
        free_tree_node(n->left);
    }
    if (NULL != n->right) {
        free_tree_node(n->right);
    }
    free(n);
}

static void free_tree(QINode *root)
{
    assert(NULL != root);

    free_tree_node(root);
}
#endif /* !NO_NEED_TO_FREE */

static int eval_tree(QINode *root, uint32_t i)
{
    assert(NULL != root);

    return available_nodes[root->type].eval(root, i);
}

static void compile_start_states(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(available_nodes); i++) {
        const char *p;

        for (p = available_nodes[i].characters; '\0' != *p; p++) {
            assignments[(unsigned char) *p] = i;
        }
    }
}

#define GETBIT_AT(var, pos) \
    !!(var & (1 << pos))

#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif /* !CHAR_BIT */

#undef BITMASK /* conflict with utils/varbit.h */
#define BITSLOT(b) ((b) / CHAR_BIT)
#define BITMASK(b) (1 << (((b) + 4) % CHAR_BIT))

#define SETBIT_AT(var, offset, pos) \
    (var[offset + BITSLOT(pos)] |= BITMASK(pos))

#include <netinet/in.h>
#define WRITE_UINT32(var, var_len, value) \
    do { \
        *((uint32_t *) (var + var_len)) = htonl(value); \
        var_len += sizeof(uint32_t); \
    } while (0);

#define BYTE_LENGTH(nb) \
    ((nb + CHAR_BIT - 1) / CHAR_BIT)

#include "hashtable-int.h"

static uint8_t *compute_hash(void *parent, ParseResult *result, uint8_t *all_true, uint8_t *all_false)
{
    int s, b;
    uint8_t *h;
    HashNode *n;
    uint32_t i, l;
    size_t h_size, h_len;

    h_len = 0;
    *all_false = *all_true = TRUE;
    h_size = sizeof(uint32_t) + hashtable_size(result->symbols) * sizeof(uint32_t) + BYTE_LENGTH((1U << hashtable_size(result->symbols)));
    h = (uint8_t *) allocate_buffer(parent, h_size);
    WRITE_UINT32(h, h_len, hashtable_size(result->symbols));
    //for (s = 0, n = result->symbols->gHead; NULL != n; n = n->gNext, s++) {
    for (s = 0, n = result->symbols->gTail; NULL != n; n = n->gPrev, s++) {
        *((uint32_t *) n->data) = 1U << s;
    }
    for (n = result->symbols->gHead; NULL != n; n = n->gNext) {
        WRITE_UINT32(h, h_len, n->hash);
#ifdef MAXIMAL_OUTPUT
        printf(" %4d "/*"(0x%X)"*/, n->hash/*, *((uint32_t *) n->data)*/);
#endif /* MAXIMAL_OUTPUT */
    }
#ifdef MAXIMAL_OUTPUT
    printf(" | Result ");
    printf("\n");
#endif /* MAXIMAL_OUTPUT */
    for (i = 0, l = 1U << hashtable_size(result->symbols); i < l; i++) {
#ifdef MAXIMAL_OUTPUT
        for (s = 0, n = result->symbols->gHead; NULL != n; n = n->gNext, s++) {
            printf(" %4d ", !!(i & *((uint32_t *) n->data))/*GETBIT_AT(i, s)*/);
        }
#endif /* MAXIMAL_OUTPUT */
        if ((b = eval_tree(result->root, i))) {
            SETBIT_AT(h, h_len, i);
        }
        *all_true &= b;
        *all_false &= ~b;
#ifdef MAXIMAL_OUTPUT
        printf(" | %4d \n", b);
#endif /* MAXIMAL_OUTPUT */
    }
    if (*all_true || *all_false) {
#ifndef NO_NEED_TO_FREE
        free(h);
#endif /* !NO_NEED_TO_FREE */
        h_len = 0;
        h = (uint8_t *) allocate_buffer(parent, sizeof(uint32_t) + 1);
        WRITE_UINT32(h, h_len, 0);
        h[h_len] = *all_true ? 0xFF : 0x00;
    }

    return h;
}

#ifdef POSTGRESQL

static char *allocate_buffer(void *parent, size_t h_size)
{
    char *h;
    bytea *ba;

    h_size += VARHDRSZ;
    ba = (bytea *) palloc0(h_size);
    SET_VARSIZE(ba, h_size);
    h = VARDATA_ANY(ba);
    *((Datum *) parent) = PointerGetDatum(ba);

    return h;
}

# define PG_RETVAL_NULL() \
    do { \
        fcinfo->isnull = true; \
        retval = (Datum) 0; \
    } while (0);

# define PG_RETVAL_VARLENA(retval, value) \
    do { \
        fcinfo->isnull = false; \
        retval = PointerGetDatum(value); \
    } while (0);

Datum compile_query_int(PG_FUNCTION_ARGS)
{
    char *expr;
    text *texpr;
    Datum retval;
    size_t expr_len;
    ParseResult result;
    uint8_t all_true, all_false;
    bool throw_false, throw_true;

    texpr = PG_GETARG_TEXT_P(0);
    throw_false = PG_GETARG_BOOL(1);
    throw_true = PG_GETARG_BOOL(2);
    expr_len = VARSIZE(texpr) - VARHDRSZ;
    expr = VARDATA(texpr);

    PG_RETVAL_NULL();
    if (!parse(expr, expr + expr_len, &result)) {
        goto end;
    }
    if (hashtable_size(result.symbols) > intarray_query_int_max_symbols) {
        ereport(
            ERROR,
            (
                errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                errmsg("query_int exceeds the maximum of symbols allowed by 'intarray.query_int.max_symbols' GUC (%d)", intarray_query_int_max_symbols)
            )
        );
        goto end;
    }

    fcinfo->isnull = false;
    compute_hash(&retval, &result, &all_true, &all_false);
    if (throw_false && all_false) {
        ereport(
            ERROR,
            (
                errcode(ERRCODE_DATA_EXCEPTION),
                errmsg("query_int is known to be always false")
            )
        );
        PG_RETVAL_NULL();
        pfree(DatumGetPointer(retval));
        goto end;
    }
    if (throw_true && all_true) {
        ereport(
            ERROR,
            (
                errcode(ERRCODE_DATA_EXCEPTION),
                errmsg("query_int is known to be always true")
            )
        );
        PG_RETVAL_NULL();
        pfree(DatumGetPointer(retval));
        goto end;
    }

end:
# ifndef NO_NEED_TO_FREE
    hashtable_destroy(result.symbols);
    if (NULL != result.root) {
        free_tree(result.root);
    }
# endif /* !NO_NEED_TO_FREE */

    return retval;
}

void _PG_init(void)
{
    compile_start_states();

    DefineCustomIntVariable(
        "intarray.query_int.max_symbols",
        gettext_noop("maximum number of integers in a query_int."),
        gettext_noop("The default value is 16."),
        &intarray_query_int_max_symbols,
        16, 2, (sizeof(int32) * CHAR_BIT - 1),
        PGC_USERSET, 0,
# if PG_VERSION_NUM >= 90100
        NULL,
# endif /* PostgreSQL >= 9.1.0 */
        NULL,
        NULL
    );
    DefineCustomIntVariable(
        "intarray.query_int.max_stack_size",
        gettext_noop("maximum stack size for query_int parsing."),
        gettext_noop("The default value is 256."),
        &intarray_query_int_max_stack_size,
        256, 0, INT_MAX,
        PGC_USERSET, 0,
# if PG_VERSION_NUM >= 90100
        NULL,
# endif /* PostgreSQL >= 9.1.0 */
        NULL,
        NULL
    );
}

#else

static char *allocate_buffer(void *parent, size_t h_size)
{
    char *h;

    h_size += 1;
    h = mem_new_n(*h, h_size);
    memset(h, 0, h_size);
    *((size_t *) parent) = h_size;

    return h;
}

static void print_tree_node(QINode *n, int ident)
{
    if (NULL != n->left) {
        print_tree_node(n->left, ident + 1);
    }
    printf("%*c%s\n", ident * 4, ' ', available_nodes[n->type].name);
    if (NULL != n->right) {
        print_tree_node(n->right, ident + 1);
    }
}

static void print_tree(QINode *root)
{
    assert(NULL != root);

    print_tree_node(root, 0);
}

# ifndef EXIT_USAGE
#  define EXIT_USAGE -2
# endif /* !EXIT_USAGE */
static void usage(void)
{
    fprintf(stderr, "%s: [EXPR]...\n", "query_int_parser");
    exit(EXIT_USAGE);
}

int strcmp_l(
    const char *str1, size_t str1_len,
    const char *str2, size_t str2_len
) {
    if (str1 != str2) {
        size_t min_len;

        if (str2_len < str1_len) {
            min_len = str2_len;
        } else {
            min_len = str1_len;
        }
        while (min_len--/* > 0*/) {
            if (*str1 != *str2) {
                return (unsigned char) *str1 - (unsigned char) *str2;
            }
            ++str1, ++str2;
        }
    }

    return str1_len - str2_len;
}

static const char hexdigits[] = "0123456789ABCDEF";

int main(int argc, char **argv)
{
    uint8_t **h;
    int a, i, ret;
    size_t *h_size;
    ParseResult result;
    uint8_t all_true, all_false;

    if (argc < 2) {
        usage();
    }
    --argc;
    ++argv;
    ret = EXIT_SUCCESS;
    compile_start_states();
    h = mem_new_n(*h, argc);
    h_size = mem_new_n(*h_size, argc);
    for (a = 0; a < argc && EXIT_SUCCESS == ret; a++) {
        h[a] = NULL;
    }
    for (a = 0; a < argc; a++) {
        const char * const end = argv[a] + strlen(argv[a]);

        printf("EXPR = %.*s\n", end - argv[a], argv[a]);
        printf("=========\n");
        if (!parse(argv[a], end, &result)) {
            ret = EXIT_FAILURE;
            goto end;
        }
        if (hashtable_size(result.symbols) > (sizeof(uint32_t) * CHAR_BIT - 1)) {
            fprintf(stderr, "too many symbols, max is %ld\n", sizeof(uint32_t) * CHAR_BIT - 1);
            ret = EXIT_FAILURE;
            goto end;
        }
        printf("=========\n");
        print_tree(result.root);
        printf("=========\n");
        h[a] = compute_hash(&h_size[a], &result, &all_true, &all_false);
        printf("H = ");
        for (i = 0; i < h_size[a]; i++) {
            printf("%02X", h[a][i]);
        }
        printf("\n");
        if (all_true) {
            fprintf(stderr, "WARNING: expression '%s' is known to be (always) true\n", argv[a]);
        }
        if (all_false) {
            fprintf(stderr, "WARNING: expression '%s' is known to be (always) false\n", argv[a]);
        }
end:
        hashtable_destroy(result.symbols);
        if (NULL != result.root) {
            free_tree(result.root);
        }
    }

    printf("=========\n");
    for (a = 0; a < argc; a++) {
        for (i = a + 1; i < argc; i++) {
            printf("%s %c= %s\n", argv[a], 0 == strcmp_l(h[a], h_size[a], h[i], h_size[i]) ? '=' : '!', argv[i]);
        }
    }
    for (a = 0; a < argc; a++) {
        if (NULL != h[a]) {
            free(h[a]);
        }
    }
    free(h_size);
    free(h);

    return ret;
}

#endif /* POSTGRESQL */
