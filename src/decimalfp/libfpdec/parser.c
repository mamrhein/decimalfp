/* ---------------------------------------------------------------------------
Name:        parser.c

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <ctype.h>
#include <stddef.h>

#include "helper_macros.h"
#include "parser.h"

/*****************************************************************************
*  Functions
*****************************************************************************/

static inline void
fill_in_digits(dec_repr_t *dec_repr, const char *dec_chars,
               const size_t n_dec_digits) {
    const char *curr_char = dec_chars;
    dec_digit_t *curr_digit = dec_repr->coeff + dec_repr->n_dec_digits;
    for (size_t i = 0; i < n_dec_digits; ++i) {
        *curr_digit = *curr_char - '0';
        curr_char++;
        curr_digit++;
    }
    dec_repr->n_dec_digits += n_dec_digits;
}

// parse for a Decimal
// [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or
// [+|-].<frac>[<e|E>[+|-]<exp>].
error_t
parse_ascii_dec_literal(dec_repr_t *result, const char *literal) {
    const char *curr_char = literal;
    const char *int_part = NULL;
    const char *signif_int_part = NULL;
    ptrdiff_t len_int_part = 0;
    const char *frac_part = NULL;
    ptrdiff_t len_frac_part = 0;

    while isspace(*curr_char) {
        curr_char++;
    }
    if (*curr_char == 0) return FPDEC_INVALID_DECIMAL_LITERAL;

    result->negative = false;
    result->exp = 0;
    result->n_dec_digits = 0;

    switch (*curr_char) {
        case '-':
            result->negative = true;
        case '+':
            curr_char++;
    }
    int_part = curr_char;
    while (*curr_char == '0') {
        curr_char++;
    }
    signif_int_part = curr_char;
    while (isdigit(*curr_char)) {
        curr_char++;
    }
    len_int_part = curr_char - signif_int_part;
    if (*curr_char == '.') {
        curr_char++;
        frac_part = curr_char;
        while (isdigit(*curr_char)) {
            curr_char++;
        }
        len_frac_part = curr_char - frac_part;
    }
    if (len_int_part == 0 && len_frac_part == 0) {
        if (*int_part == '0') {
            signif_int_part = int_part;
            len_int_part = 1;
        }
        else
            return FPDEC_INVALID_DECIMAL_LITERAL;
    }
    if (*curr_char == 'e' || *curr_char == 'E') {
        int sign = 1;
        int exp = 0;
        curr_char++;
        switch (*curr_char) {
            case '-':
                sign = -1;
            case '+':
                curr_char++;
                break;
            default:
                if (!isdigit(*curr_char))
                    return FPDEC_INVALID_DECIMAL_LITERAL;
        }
        while isdigit(*curr_char) {
            exp = exp * 10 + (*curr_char - '0');
            curr_char++;
        }
        result->exp = sign * exp;
    }
    while isspace(*curr_char) {
        curr_char++;
    }
    if (*curr_char != 0)
        return FPDEC_INVALID_DECIMAL_LITERAL;
    fill_in_digits(result, signif_int_part, len_int_part);
    fill_in_digits(result, frac_part, len_frac_part);
    result->exp -= len_frac_part;
    return FPDEC_OK;
}
