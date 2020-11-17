/* ---------------------------------------------------------------------------
Name:        format_spec.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_FORMAT_SPEC_H
#define FPDEC_FORMAT_SPEC_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/*****************************************************************************
*  Types
*****************************************************************************/

typedef struct utf8c {
    uint8_t n_bytes;
    uint8_t bytes[4];
} utf8c_t;

typedef struct format_spec {
    utf8c_t fill;                   // fill character: <any character>
    char align;                     // alignment: '<' | '>' | '=' | '^'
    char sign;                      // sign formatting: '+' | '-' | ' '
    size_t min_width;               // minimum total field width
    utf8c_t thousands_sep;          // thousands separator: <any character>
    uint8_t grouping[5];            // thousands grouping
    utf8c_t decimal_point;          // decimal point: <any character>
    size_t precision;               // number of fractional digits
    char type;                      // format type: 'f' | 'F' | 'n' | '%'
} format_spec_t;

/*****************************************************************************
*  Constants
*****************************************************************************/

extern const format_spec_t DFLT_FORMAT;

/*****************************************************************************
*  Functions
*****************************************************************************/

static inline int8_t
utf8c_len(const uint8_t *cp) {
    int8_t n;
    if (*cp == '\0')
        return 0;
    if (*cp < 128)
        return 1;
    if ((*cp >> 5U) == 6)
        n = 2;
    else if ((*cp >> 4U) == 14)
        n = 3;
    else if ((*cp >> 3U) == 30)
        n = 4;
    else
        return -1;
    for (const uint8_t *stop = cp + n; ++cp < stop;)
        if ((*cp >> 6U) != 2)
            return -1;
    return n;
}

static inline size_t
utf8_strlen(const uint8_t *cp) {
    size_t n = 0;
    uint8_t l;
    while (*cp != '\0') {
        if (*cp < 128) {
            ++n;
            ++cp;
        }
        else {
            if ((*cp >> 5U) == 6)
                l = 2;
            else if ((*cp >> 4U) == 14)
                l = 3;
            else if ((*cp >> 3U) == 30)
                l = 4;
            else
                return SIZE_MAX;
            for (const uint8_t *stop = cp + l; ++cp < stop;)
                if ((*cp >> 6U) != 2)
                    return SIZE_MAX;
            n += l;
        }
    }
    return n;
}

// Format string:
// [[fill]align][sign][0][min_width][,][.precision][type]
int
parse_format_spec(format_spec_t *spec, const uint8_t *fmt);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif //FPDEC_FORMAT_SPEC_H
