#ifndef COMMON_H

# define COMMON_H

# ifndef __has_attribute
#  define __has_attribute(x) 0
# endif /* !__has_attribute */

# if __GNUC__ || __has_attribute(unused)
#  define UNUSED(x) UNUSED_ ## x __attribute__((unused))
# else
#  define UNUSED
# endif /* UNUSED */

# ifdef POSTGRESQL
#  include "postgres.h"
# endif /* POSTGRESQL */

# ifdef DEBUG
#  undef NDEBUG
#  include <stdarg.h>
#  ifdef POSTGRESQL /* ERRCODE_INTERNAL_ERROR */
#   define debug(fmt, ...) \
        ereport(LOG, (errcode(ERRCODE_WARNING), errmsg(fmt, ## __VA_ARGS__)))
#  else
#   define MAXIMAL_OUTPUT
#   define debug(fmt, ...) \
        fprintf(stderr, fmt "\n", ## __VA_ARGS__)
#  endif /* POSTGRESQL */
# else
#  ifndef NDEBUG
#   define NDEBUG
#  endif /* !NDEBUG */
#  define debug(fmt, ...) \
    /* NOP */
# endif /* DEBUG */
# include <assert.h>

# ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
# endif /* !MAX */

# ifndef MIN
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
# endif /* !MIN */

# define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
# define STR_LEN(str) (ARRAY_SIZE(str) - 1)
# define STR_SIZE(str) (ARRAY_SIZE(str))

# define HAS_FLAG(value, flag) \
    (0 != ((value) & (flag)))

# define SET_FLAG(value, flag) \
    ((value) |= (flag))

# define UNSET_FLAG(value, flag) \
    ((value) &= ~(flag))

# ifdef POSTGRESQL
#  include "utils/palloc.h"
#  define NO_NEED_TO_FREE
#  define mem_new(type)           palloc(sizeof(type))
#  define mem_new_n(type, n)      palloc(sizeof(type) * (n))
#  define mem_renew(ptr, type, n) repalloc(ptr, sizeof(type) * (n))
#  ifndef  NO_NEED_TO_FREE
#   define free(ptr)               pfree(ptr)
#   define free_func_name          pfree
#  else
#   define free(ptr)               (void) ptr
#   define free_func_name          NULL
#  endif /* !NO_NEED_TO_FREE */
# else
#  define mem_new(type)           malloc(sizeof(type))
#  define mem_new_n(type, n)      malloc(sizeof(type) * (n))
#  define mem_renew(ptr, type, n) realloc(ptr, sizeof(type) * (n))
#  define free_func_name          free
# endif /* POSTGRESQL */

# ifndef POSTGRESQL
#  ifdef __bool_true_false_are_defined
#   include <stdbool.h>
#   define TRUE true
#   define FALSE false
#  else
//# include <wtypes.h>
typedef enum
{
    FALSE = 0,
    TRUE  = 1
} bool;
#  endif /* C99 boolean */
# endif /* !POSTGRESQL */

# include <stdlib.h>
# include <stdint.h>
# include <limits.h>
# include <inttypes.h>
# ifndef PRIszu
#  define PRIszu "zu"
# endif /* !PRIszu */

typedef void (*DtorFunc)(void *);
typedef void *(*DupFunc)(const void *);
typedef int (*ForeachFunc)();

#endif /* !COMMON_H */
