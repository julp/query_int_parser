#include "common.h"
#include "parsenum.h"

#define parse_signed(type, unsigned_type, value_type_min, value_type_max) \
    ParseNumError strnto## type(const char *nptr, const char * const end, char **endptr, type *ret) { \
        char c; \
        char ***spp; \
        int negative; \
        int any, cutlim; \
        ParseNumError err; \
        unsigned_type cutoff, acc; \
 \
        acc = any = 0; \
        negative = FALSE; \
        err = PARSE_NUM_NO_ERR; \
        if (NULL == endptr) { \
            char **sp; \
 \
            sp = (char **) &nptr; \
            spp = &sp; \
        } else { \
            spp = &endptr; \
            *endptr = (char *) nptr; \
        } \
        if (**spp < end) { \
            if ('-' == ***spp) { \
                ++**spp; \
                negative = TRUE; \
            } else { \
                negative = FALSE; \
                if ('+' == ***spp) { \
                    ++**spp; \
                } \
            } \
            cutoff = negative ? (unsigned_type) - (value_type_min + value_type_max) + value_type_max : value_type_max; \
            cutlim = cutoff % 10; \
            cutoff /= 10; \
            while (**spp < end) { \
                if (***spp >= '0' && ***spp <= '9') { \
                    c = ***spp - '0'; \
                } else { \
                    err = PARSE_NUM_ERR_NON_DIGIT_FOUND; \
                    break; \
                } \
                if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) { \
                    any = -1; \
                } else { \
                    any = 1; \
                    acc *= 10; \
                    acc += c; \
                } \
                ++**spp; \
            } \
        } \
        if (any < 0) { \
            if (negative) { \
                *ret = value_type_min; \
                return PARSE_NUM_ERR_TOO_SMALL; \
            } else { \
                *ret = value_type_max; \
                return PARSE_NUM_ERR_TOO_LARGE; \
            } \
        } else if (!any && PARSE_NUM_NO_ERR == err) { \
            err = PARSE_NUM_ERR_NO_DIGIT_FOUND; \
        } else if (negative) { \
            *ret = -acc; \
        } else { \
            *ret = acc; \
        } \
 \
        return err; \
    }

// parse_signed(int8_t, uint8_t, INT8_MIN, INT8_MAX);
// parse_signed(int16_t, uint16_t, INT16_MIN, INT16_MAX);
// parse_signed(int32_t, uint32_t, INT32_MIN, INT32_MAX);
// parse_signed(int64_t, uint64_t, INT64_MIN, INT64_MAX);

#undef parse_signed

#define parse_unsigned(type, value_type_max) \
    ParseNumError strnto## type(const char *nptr, const char * const end, char **endptr, type *ret) { \
        char c; \
        char ***spp; \
        int negative; \
        int any, cutlim; \
        type cutoff, acc; \
        ParseNumError err; \
 \
        acc = any = 0; \
        negative = FALSE; \
        err = PARSE_NUM_NO_ERR; \
        if (NULL == endptr) { \
            char **sp; \
 \
            sp = (char **) &nptr; \
            spp = &sp; \
        } else { \
            spp = &endptr; \
            *endptr = (char *) nptr; \
        } \
        if (**spp < end) { \
            if ('-' == ***spp) { \
                ++**spp; \
                negative = TRUE; \
            } else { \
                negative = FALSE; \
                if ('+' == ***spp) { \
                    ++**spp; \
                } \
            } \
            cutoff = value_type_max / 10; \
            cutlim = value_type_max % 10; \
            while (**spp < end) { \
                if (***spp >= '0' && ***spp <= '9') { \
                    c = ***spp - '0'; \
                } else { \
                    err = PARSE_NUM_ERR_NON_DIGIT_FOUND; \
                    break; \
                } \
                if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) { \
                    any = -1; \
                } else { \
                    any = 1; \
                    acc *= 10; \
                    acc += c; \
                } \
                ++**spp; \
            } \
        } \
        if (any < 0) { \
            *ret = value_type_max; \
            return PARSE_NUM_ERR_TOO_LARGE; \
        } else if (!any && PARSE_NUM_NO_ERR == err) { \
            err = PARSE_NUM_ERR_NO_DIGIT_FOUND; \
        } else if (negative) { \
            *ret = -acc; \
        } else { \
            *ret = acc; \
        } \
 \
        return err; \
    }

// parse_unsigned(uint8_t, UINT8_MAX);
// parse_unsigned(uint16_t, UINT16_MAX);
parse_unsigned(uint32_t, UINT32_MAX);
// parse_unsigned(uint64_t, UINT64_MAX);

#undef parse_unsigned
