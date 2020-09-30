/*
------------------------------------------------------------------------------
Name:        fpdec.c
Purpose:     Fixed-point decimal arithmetic

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "fpdec.h"
#include "fpdec_struct.h"
#include "digit_array.h"
#include "digit_array_struct.h"
#include "parser.h"
#include "shifted_int.h"
#include "rounding_helper.h"
#include "unicode_digits.h"


/*****************************************************************************
*  Constants
*****************************************************************************/

const fpdec_t FPDEC_ZERO = {
    .dyn_alloc = false,
    .normalized = false,
    .sign = 0,
    .dec_prec = 0,
    .hi = 0,
    .lo = 0,
};
const fpdec_t FPDEC_ONE = {
    .dyn_alloc = false,
    .normalized = false,
    .sign = 1,
    .dec_prec = 0,
    .hi = 0,
    .lo = 1,
};
const fpdec_t FPDEC_MINUS_ONE = {
    .dyn_alloc = false,
    .normalized = false,
    .sign = -1,
    .dec_prec = 0,
    .hi = 0,
    .lo = 1,
};


/*****************************************************************************
*  Macros
*****************************************************************************/

#define U128_FROM_SHINT(x) {x->lo, x->hi}

#define FPDEC_IS_ZEROED(fpdec) (!FPDEC_IS_DYN_ALLOC(fpdec) && \
                                !FPDEC_IS_NORMALIZED(fpdec) && \
                                FPDEC_SIGN(fpdec) == 0 && \
                                FPDEC_DEC_PREC(fpdec) == 0 && \
                                ((fpdec_t*)fpdec)->hi == 0 && \
                                ((fpdec_t*)fpdec)->lo == 0)

#define ASSERT_FPDEC_IS_ZEROED(fpdec) assert(FPDEC_IS_ZEROED(fpdec))

#define DISPATCH_FUNC(vtab, fpdec) \
        (vtab[FPDEC_IS_DYN_ALLOC(fpdec)])(fpdec)

#define DISPATCH_FUNC_VA(vtab, fpdec, ...) \
        (vtab[FPDEC_IS_DYN_ALLOC(fpdec)])(fpdec, __VA_ARGS__)

#define DISPATCH_BIN_EXPR(vtab, x, y) \
        (vtab[((FPDEC_IS_DYN_ALLOC(x)) << 1U) + FPDEC_IS_DYN_ALLOC(y)])(x, y)

#define DISPATCH_BIN_OP(vtab, z, x, y) \
        (vtab[((FPDEC_IS_DYN_ALLOC(x)) << 1U) + FPDEC_IS_DYN_ALLOC(y)]) \
                (z, x, y)

#define DISPATCH_BIN_OP_VA(vtab, z, x, y, ...) \
        (vtab[((FPDEC_IS_DYN_ALLOC(x)) << 1U) + FPDEC_IS_DYN_ALLOC(y)]) \
                (z, x, y, __VA_ARGS__)

/*****************************************************************************
*  Functions
*****************************************************************************/

// For testing only!

void
fpdec_dump(const fpdec_t *fpdec) {
    printf("flags:\n  dyn_alloc: %d\n  normalized: %d\n",
           FPDEC_IS_DYN_ALLOC(fpdec), FPDEC_IS_NORMALIZED(fpdec));
    printf("sign: %d\n", FPDEC_SIGN(fpdec));
    printf("dec_prec: %u\n", FPDEC_DEC_PREC(fpdec));
    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        printf("exp: %d\n", FPDEC_DYN_EXP(fpdec));
        printf("n digits: %u\n", FPDEC_DYN_N_DIGITS(fpdec));
        printf("digits: ");
        for (int i = 0; i < FPDEC_DYN_N_DIGITS(fpdec); ++i) {
            printf("%lu, ", FPDEC_DYN_DIGITS(fpdec)[i]);
        }
    }
    else {
        printf("digits: %lu %u\n", fpdec->lo, fpdec->hi);
    }
    printf("\n\n");
}

// Constructor

fpdec_t *
fpdec_new() {
    return (fpdec_t *)fpdec_mem_alloc(sizeof(fpdec_t), 1);
}

// Initializer

error_t
fpdec_copy(fpdec_t *fpdec, const fpdec_t *src) {
    *fpdec = *src;
    if (src->dyn_alloc) {
        fpdec->digit_array = digits_copy(src->digit_array, 0, 0);
        if (fpdec->digit_array == NULL) MEMERROR
    }
    return FPDEC_OK;
}

error_t
fpdec_from_ascii_literal(fpdec_t *fpdec, const char *literal) {
    size_t n_chars = strlen(literal);
    dec_repr_t st_dec_repr;
    dec_repr_t *dec_repr;
    size_t n_add_zeros, n_dec_digits;
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(fpdec);

    if (n_chars == 0) ERROR(FPDEC_INVALID_DECIMAL_LITERAL)

    if (n_chars <= COEFF_SIZE_THRESHOLD) {
        dec_repr = &st_dec_repr;
    }
    else {
        dec_repr = fpdec_mem_alloc(offsetof(dec_repr_t, coeff) + n_chars, 1);
        if (dec_repr == NULL) MEMERROR
    }
    rc = parse_ascii_dec_literal(dec_repr, literal);
    if (rc != FPDEC_OK) ERROR(rc)

    fpdec->sign = dec_repr->negative ? FPDEC_SIGN_NEG : FPDEC_SIGN_POS;
    n_add_zeros = MAX(0, dec_repr->exp);
    n_dec_digits = dec_repr->n_dec_digits + n_add_zeros;
    if (n_dec_digits <= MAX_N_DEC_DIGITS_IN_SHINT &&
        -dec_repr->exp <= MAX_DEC_PREC_FOR_SHINT) {
        rc = shint_from_dec_coeff(&fpdec->lo, &fpdec->hi,
                                  dec_repr->coeff, dec_repr->n_dec_digits,
                                  n_add_zeros);
        if (rc == FPDEC_OK) {
            fpdec->dyn_alloc = false;
            fpdec->normalized = false;
            fpdec->dec_prec = MAX(0, -dec_repr->exp);
            if (fpdec->lo == 0 && fpdec->hi == 0) {
                fpdec->sign = FPDEC_SIGN_ZERO;
            }
            goto EXIT;
        }
    }
    rc = digits_from_dec_coeff_exp(&(fpdec->digit_array), &(fpdec->exp),
                                   dec_repr->n_dec_digits, dec_repr->coeff,
                                   dec_repr->exp);
    if (rc != FPDEC_OK)
        goto ERROR;
    fpdec->exp += digits_eliminate_trailing_zeros(fpdec->digit_array);
    fpdec->dyn_alloc = true;
    if (FPDEC_DYN_N_DIGITS(fpdec) > 0) {
        fpdec->normalized = true;
    }
    else {      // corner case: result == 0
        fpdec_reset_to_zero(fpdec, 0);
    }
    fpdec->dec_prec = MAX(0, -(dec_repr->exp));
    goto EXIT;

ERROR:
    fpdec_reset_to_zero(fpdec, 0);

EXIT:
    if (dec_repr != &st_dec_repr) {
        fpdec_mem_free(dec_repr);
    }
    return rc;
}

error_t
fpdec_from_unicode_literal(fpdec_t *fpdec, const wchar_t *literal) {
    size_t size = wcslen(literal);
    char *buf, *ch;
    error_t rc;

    if (size == 0) ERROR(FPDEC_INVALID_DECIMAL_LITERAL)

    ch = buf = fpdec_mem_alloc(size + 1, sizeof(char));
    if (buf == NULL) MEMERROR

    for (; iswspace(*literal); ++literal);
    for (; *literal != 0; ++literal, ++ch) {
        if (*literal > 255) {
            *ch = lookup_unicode_digit(*literal);
            if (*ch == -1) {
                fpdec_mem_free(buf);
                ERROR(FPDEC_INVALID_DECIMAL_LITERAL)
            }
        }
        else
            *ch = (char)*literal;
    }
    *ch = '\0';
    rc = fpdec_from_ascii_literal(fpdec, buf);
    fpdec_mem_free(buf);
    return rc;
}

error_t
fpdec_from_long_long(fpdec_t *fpdec, const long long val) {
    ASSERT_FPDEC_IS_ZEROED(fpdec);

    if (val > 0) {
        fpdec->sign = FPDEC_SIGN_POS;
        fpdec->lo = val;
    }
    else if (val < 0) {
        fpdec->sign = FPDEC_SIGN_NEG;
        fpdec->lo = -val;
    }
    return FPDEC_OK;
}

error_t
fpdec_from_sign_digits_exp(fpdec_t *fpdec, fpdec_sign_t sign, size_t n_digits,
                           const fpdec_digit_t *digits, fpdec_exp_t exp) {
    ASSERT_FPDEC_IS_ZEROED(fpdec);
    assert(sign == FPDEC_SIGN_POS || sign == FPDEC_SIGN_NEG);
    assert(n_digits > 0);
    assert(digits != NULL);

    fpdec->sign = sign;

    // eliminate leading zeros
    for (; n_digits > 0 && digits[n_digits - 1] == 0; --n_digits);
    // eliminate trailing zeros
    for (; n_digits > 0 && *digits == 0; --n_digits, ++exp, ++digits);

    // all digits = 0  =>  result = 0
    if (n_digits == 0)
        return FPDEC_OK;

    // one digit fits into a shint
    if (n_digits == 1 && exp == 0) {
        fpdec->lo = *digits;
        return FPDEC_OK;
    }

    fpdec->dyn_alloc = true;
    fpdec->normalized = true;
    fpdec->dec_prec = exp >= 0 ? 0 : -exp * DEC_DIGITS_PER_DIGIT;
    fpdec->exp = exp;
    return digits_from_digits(&(fpdec->digit_array), digits, n_digits);
}

// Properties

static int
fpdec_shint_magnitude(const fpdec_t *fpdec) {
    if (fpdec->hi == 0)
        return U64_MAGNITUDE(fpdec->lo) - fpdec->dec_prec;
    else
        return U128_MAGNITUDE(fpdec->lo, fpdec->hi) - fpdec->dec_prec;
}

static int
fpdec_dyn_magnitude(const fpdec_t *fpdec) {
    int rel_pos_radix_point = FPDEC_DYN_N_DIGITS(fpdec) +
                              FPDEC_DYN_EXP(fpdec);
    fpdec_digit_t most_signif_digit = FPDEC_DYN_MOST_SIGNIF_DIGIT(fpdec);
    assert(most_signif_digit != 0);
    return (rel_pos_radix_point - 1) * DEC_DIGITS_PER_DIGIT +
           U64_MAGNITUDE(most_signif_digit);
}

typedef int (*v_magnitude)(const fpdec_t *);

const v_magnitude vtab_magnitude[2] = {fpdec_shint_magnitude,
                                       fpdec_dyn_magnitude};

int
fpdec_magnitude(const fpdec_t *fpdec) {
    if (FPDEC_EQ_ZERO(fpdec)) ERROR_RETVAL(ERANGE, -1)
    return DISPATCH_FUNC(vtab_magnitude, fpdec);
}

// Comparison

// Pre-condition: magnitude(x) == magnitude(y)
static int
fpdec_cmp_abs_shint_to_shint(const fpdec_t *x, const fpdec_t *y) {
    uint128_t x_shint = U128_FROM_SHINT(x);
    uint128_t y_shint = U128_FROM_SHINT(y);

    return shint_cmp_abs(x_shint, x->dec_prec, y_shint, y->dec_prec);
}

// Pre-condition: magnitude(x) == magnitude(y)
static int
fpdec_cmp_abs_shint_to_dyn(const fpdec_t *x, const fpdec_t *y) {
    fpdec_digit_t x_digits[3];
    int n_trailing_zeros_skipped;
    fpdec_n_digits_t x_n_digits;

    x_n_digits = shint_to_digits(x_digits, &n_trailing_zeros_skipped, RADIX,
                                 x->lo, x->hi, FPDEC_DEC_PREC(x));
    return digits_cmp(x_digits, x_n_digits,
                      FPDEC_DYN_DIGITS(y), FPDEC_DYN_N_DIGITS(y));
}

// Pre-condition: magnitude(x) == magnitude(y)
static int
fpdec_cmp_abs_dyn_to_shint(const fpdec_t *x, const fpdec_t *y) {
    fpdec_digit_t y_digits[3];
    int n_trailing_zeros_skipped;
    fpdec_n_digits_t y_n_digits;

    y_n_digits = shint_to_digits(y_digits, &n_trailing_zeros_skipped, RADIX,
                                 y->lo, y->hi, FPDEC_DEC_PREC(y));
    return digits_cmp(FPDEC_DYN_DIGITS(x), FPDEC_DYN_N_DIGITS(x),
                      y_digits, y_n_digits);
}

// Pre-condition: magnitude(x) == magnitude(y)
static int
fpdec_cmp_abs_dyn_to_dyn(const fpdec_t *x, const fpdec_t *y) {
    return digits_cmp(FPDEC_DYN_DIGITS(x), FPDEC_DYN_N_DIGITS(x),
                      FPDEC_DYN_DIGITS(y), FPDEC_DYN_N_DIGITS(y));
}

typedef int (*v_cmp)(const fpdec_t *, const fpdec_t *);

const v_cmp vtab_cmp[4] = {fpdec_cmp_abs_shint_to_shint,
                           fpdec_cmp_abs_shint_to_dyn,
                           fpdec_cmp_abs_dyn_to_shint,
                           fpdec_cmp_abs_dyn_to_dyn};

int
fpdec_compare(const fpdec_t *x, const fpdec_t *y, const bool ignore_sign) {
    fpdec_sign_t x_sign = FPDEC_SIGN(x);
    fpdec_sign_t y_sign = FPDEC_SIGN(y);
    int x_magn, y_magn;

    if (ignore_sign) {
        if (x_sign == 0)
            return y_sign ? -1 : 0;
        if (y_sign == 0)
            return 1;
        x_sign = FPDEC_SIGN_POS;
    }
    else {
        int8_t cmp = CMP(x_sign, y_sign);
        if (cmp != 0 || x_sign == 0)
            return cmp;
    }

    // here: x != 0 and y != 0
    x_magn = fpdec_magnitude(x);
    y_magn = fpdec_magnitude(y);
    if (x_magn != y_magn)
        return CMP(x_magn, y_magn) * x_sign;

    return DISPATCH_BIN_EXPR(vtab_cmp, x, y) * x_sign;
}

// Converter

error_t
fpdec_neg(fpdec_t *fpdec, const fpdec_t *src) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(fpdec);

    rc = fpdec_copy(fpdec, src);
    if (rc == ENOMEM) MEMERROR
    fpdec->sign = -src->sign;
    return FPDEC_OK;
}

static error_t
fpdec_shint_to_dyn(fpdec_t *fpdec) {
    fpdec_digit_t digits[3];
    int n_trailing_zeros;
    fpdec_n_digits_t n_digits;
    error_t rc;

    assert(!FPDEC_IS_DYN_ALLOC(fpdec));

    n_digits = shint_to_digits(digits, &n_trailing_zeros, RADIX, fpdec->lo,
                               fpdec->hi, FPDEC_DEC_PREC(fpdec));
    rc = digits_from_digits(&fpdec->digit_array, digits, n_digits);
    if (rc == FPDEC_OK) {
        fpdec->dyn_alloc = true;
        fpdec->normalized = true;
        fpdec->exp = n_trailing_zeros -
                     CEIL(FPDEC_DEC_PREC(fpdec), DEC_DIGITS_PER_DIGIT);
    }
    return rc;
}

static error_t
fpdec_copy_shint_as_dyn(fpdec_t *cpy, const fpdec_t *src) {
    error_t rc;

    assert(!FPDEC_IS_DYN_ALLOC(src));

    rc = fpdec_copy(cpy, src);
    if (rc == FPDEC_OK)
        rc = fpdec_shint_to_dyn(cpy);
    return rc;
}

static void
fpdec_dyn_normalize(fpdec_t *fpdec) {
    fpdec_dec_prec_t dec_prec = FPDEC_DEC_PREC(fpdec);

    assert(FPDEC_IS_DYN_ALLOC(fpdec));

    while (FPDEC_DYN_N_DIGITS(fpdec) > 0 &&
           FPDEC_DYN_MOST_SIGNIF_DIGIT(fpdec) == 0)
        (FPDEC_DYN_N_DIGITS(fpdec))--;
    if (FPDEC_DYN_N_DIGITS(fpdec) == 0) {
        fpdec_reset_to_zero(fpdec, dec_prec);
        return;
    }
    FPDEC_DYN_EXP(fpdec) +=
        digits_eliminate_trailing_zeros(fpdec->digit_array);
    if (FPDEC_DYN_N_DIGITS(fpdec) == 0) {
        fpdec_reset_to_zero(fpdec, dec_prec);
        return;
    }
    else
        fpdec->normalized = true;

    // try to transform dyn fpdec to shifted int
    if (dec_prec <= MAX_DEC_PREC_FOR_SHINT && FPDEC_DYN_EXP(fpdec) <= 1) {
        size_t n_dec_digits = MAX(fpdec_magnitude(fpdec), 0) + dec_prec;
        if (n_dec_digits <= MAX_N_DEC_DIGITS_IN_SHINT) {
            fpdec_sign_t sign = FPDEC_SIGN(fpdec);
            uint128_t shint = {0, 0};
            uint128_t f = {0, 0};
            fpdec_n_digits_t n_digits = FPDEC_DYN_N_DIGITS(fpdec);
            fpdec_n_digits_t digit_idx = 0;
            fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
            uint64_t dec_shift = _10_POW_N(dec_prec);
            switch FPDEC_DYN_EXP(fpdec) {
                case -1:
                    u64_mul_u64(&shint, digits[digit_idx], dec_shift);
                    u128_idiv_u64(&shint, RADIX);
                    if (++digit_idx == n_digits)
                        break;
                case 0:
                    u64_mul_u64(&f, digits[digit_idx], dec_shift);
                    u128_iadd_u128(&shint, &f);
                    if (++digit_idx == n_digits)
                        break;
                case 1:
                    u64_mul_u64(&f, digits[digit_idx], dec_shift);
                    u128_imul_u64(&f, RADIX);
                    u128_iadd_u128(&shint, &f);
                    if (++digit_idx == n_digits)
                        break;
                default:
                    assert(digit_idx == n_digits);
            }
            if (U128_FITS_SHINT(shint)) {
                fpdec_reset_to_zero(fpdec, dec_prec);
                FPDEC_SIGN(fpdec) = sign;
                fpdec->lo = shint.lo;
                fpdec->hi = shint.hi;
            }
        }
    }
}

error_t
fpdec_normalize_prec(fpdec_t *fpdec) {
    fpdec_dec_prec_t dec_prec = FPDEC_DEC_PREC(fpdec);
    if (dec_prec == 0)
        return FPDEC_OK;

    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        int32_t exp = FPDEC_DYN_EXP(fpdec);
        if (exp >= 0)
            FPDEC_DEC_PREC(fpdec) = 0;
        else {
            int32_t n_dec_frac_digits = -exp * DEC_DIGITS_PER_DIGIT;
            if (n_dec_frac_digits < dec_prec) {
                FPDEC_DEC_PREC(fpdec) = n_dec_frac_digits;
                fpdec_digit_t digit = *FPDEC_DYN_DIGITS(fpdec);
                while (digit % 10 == 0) {
                    --FPDEC_DEC_PREC(fpdec);
                    digit /= 10;
                }
            }
        }
    }
    else {
        uint128_t ui = U128_FROM_SHINT(fpdec);
        FPDEC_DEC_PREC(fpdec) -=
            u128_eliminate_trailing_zeros(&ui, FPDEC_DEC_PREC(fpdec));
        fpdec->hi = ui.hi;
        fpdec->lo = ui.lo;
    }
    return FPDEC_OK;
}

static error_t
fpdec_dyn_adjust_to_prec(fpdec_t *fpdec, int32_t dec_prec,
                         const enum FPDEC_ROUNDING_MODE rounding) {
    int32_t radix_point_at = -FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT;

    if (dec_prec >= FPDEC_DEC_PREC(fpdec) || dec_prec >= radix_point_at) {
        // no need to adjust digits
        FPDEC_DEC_PREC(fpdec) = dec_prec;
    }
    else {
        // need to shorten / round digits
        int32_t dec_shift = radix_point_at - dec_prec;
        int32_t n_dec_digits = FPDEC_DYN_N_DIGITS(fpdec) *
                               DEC_DIGITS_PER_DIGIT;
        if (dec_shift > n_dec_digits) {
            fpdec_digit_t quant;
            FPDEC_DYN_EXP(fpdec) += dec_shift / DEC_DIGITS_PER_DIGIT;
            dec_shift %= DEC_DIGITS_PER_DIGIT;
            quant = _10_POW_N(dec_shift);
            if (round_qr(FPDEC_SIGN(fpdec), 0UL, 0UL, true, quant,
                         rounding) == 0UL) {
                FPDEC_DYN_N_DIGITS(fpdec) = 0;
            }
            else {
                FPDEC_DYN_N_DIGITS(fpdec) = 1;
                fpdec->digit_array->digits[0] = quant;
            }
        }
        else {
            bool carry = digits_round(fpdec->digit_array,
                                      FPDEC_SIGN(fpdec),
                                      dec_shift, rounding);
            if (carry) {
                // total carry-over
                FPDEC_DYN_EXP(fpdec) += FPDEC_DYN_N_DIGITS(fpdec);
                fpdec->digit_array->digits[0] = 1UL;
                FPDEC_DYN_N_DIGITS(fpdec) = 1;
            }
        }
        FPDEC_DEC_PREC(fpdec) = dec_prec >= 0 ? dec_prec : 0;
        fpdec_dyn_normalize(fpdec);
    }
    return FPDEC_OK;
}

static error_t
fpdec_shint_adjust_to_prec(fpdec_t *fpdec, const int32_t dec_prec,
                           const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    int32_t adj_to = dec_prec;
    int32_t dec_shift = dec_prec - FPDEC_DEC_PREC(fpdec);
    uint128_t shifted = U128_FROM_SHINT(fpdec);

    if (-dec_shift > MAX_N_DEC_DIGITS_IN_SHINT) {
        fpdec_copy(fpdec, &FPDEC_ZERO);
        return FPDEC_OK;
    }

    u128_idecshift(&shifted, FPDEC_SIGN(fpdec), dec_shift, rounding);
    while (adj_to < 0) {
        // shift back
        dec_shift = MIN(-adj_to, UINT64_10_POW_N_CUTOFF);
        u128_imul_u64(&shifted, _10_POW_N(dec_shift));
        adj_to += dec_shift;
    }

    if (!U128_FITS_SHINT(shifted)) {
        rc = fpdec_shint_to_dyn(fpdec);
        if (rc != FPDEC_OK)
            return rc;
        return fpdec_dyn_adjust_to_prec(fpdec, dec_prec, rounding);
    }
    else {
        if (shifted.lo > 0 || shifted.hi > 0) {
            fpdec->lo = shifted.lo;
            fpdec->hi = shifted.hi;
        }
        else {
            FPDEC_SIGN(fpdec) = FPDEC_SIGN_ZERO;
            fpdec->lo = 0;
            fpdec->hi = 0;
        }
    }
    FPDEC_DEC_PREC(fpdec) = adj_to;
    return FPDEC_OK;
}

typedef error_t (*v_adjust_to_prec)(fpdec_t *, int32_t,
                                    const enum FPDEC_ROUNDING_MODE);

const v_adjust_to_prec vtab_adjust_to_prec[2] = {fpdec_shint_adjust_to_prec,
                                                 fpdec_dyn_adjust_to_prec};

error_t
fpdec_adjust(fpdec_t *fpdec, int32_t dec_prec,
             enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;

    if (ABS(dec_prec) > FPDEC_MAX_DEC_PREC) ERROR(FPDEC_PREC_LIMIT_EXCEEDED)

    if (FPDEC_DEC_PREC(fpdec) == dec_prec)
        return FPDEC_OK;

    if (FPDEC_EQ_ZERO(fpdec)) {
        FPDEC_DEC_PREC(fpdec) = dec_prec;
        return FPDEC_OK;
    }

    if (dec_prec > MAX_DEC_PREC_FOR_SHINT && !FPDEC_IS_DYN_ALLOC(fpdec)) {
        rc = fpdec_shint_to_dyn(fpdec);
        if (rc != FPDEC_OK)
            return rc;
    }

    return DISPATCH_FUNC_VA(vtab_adjust_to_prec, fpdec, dec_prec, rounding);
}

error_t
fpdec_adjusted(fpdec_t *fpdec, const fpdec_t *src, int32_t dec_prec,
               enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(fpdec);

    rc = fpdec_copy(fpdec, src);
    if (rc == ENOMEM) MEMERROR

    rc = fpdec_adjust(fpdec, dec_prec, rounding);
    if (rc != FPDEC_OK)
        fpdec_reset_to_zero(fpdec, 0);
    return rc;
}

static inline char *
fill_in_leading_digit(char *buf, const fpdec_digit_t digit) {
    char dec_digits[DEC_DIGITS_PER_DIGIT];
    char *ch = dec_digits;
    fpdec_digit_t t = digit;
    while (t > 0) {
        *(ch++) = '0' + (t % 10);
        t /= 10;
    }
    while (--ch >= dec_digits)
        *(buf++) = *ch;
    return buf;
}

static inline char *
fill_in_digit(char *buf, const fpdec_digit_t digit, const int n) {
    fpdec_digit_t t = digit;
    char *ch;
    ch = buf = buf + n;
    for (int i = 0; i < n; i++) {
        *(--ch) = '0' + (t % 10);
        t /= 10;
    }
    return buf;
}

static inline char *
fill_in_zeros(char *ch, const int n) {
    for (int i = 0; i < n; i++)
        *(ch++) = '0';
    return ch;
}

static char *
fpdec_dyn_as_ascii_literal(const fpdec_t *fpdec,
                           const bool no_trailing_zeros) {
    fpdec_sign_t sign = FPDEC_SIGN(fpdec);
    fpdec_dec_prec_t dec_prec = FPDEC_DEC_PREC(fpdec);
    fpdec_exp_t exp = FPDEC_DYN_EXP(fpdec);
    fpdec_n_digits_t n_int_digits, n_frac_digits;
    size_t n_dec_trailing_int_zeros, n_dec_trailing_frac_zeros;
    size_t n_dec_fill_zeros;
    size_t n_dec_frac_digits, d_adjust;
    size_t max_n_chars;
    char *buf;
    char *ch;
    fpdec_digit_t *digit =
        FPDEC_DYN_DIGITS(fpdec) + FPDEC_DYN_N_DIGITS(fpdec) - 1;

    if (exp >= 0) {
        n_int_digits = FPDEC_DYN_N_DIGITS(fpdec);
        n_frac_digits = 0;
    }
    else {
        n_frac_digits = MIN(FPDEC_DYN_N_DIGITS(fpdec), -exp);
        n_int_digits = FPDEC_DYN_N_DIGITS(fpdec) - n_frac_digits;
    }
    n_dec_trailing_int_zeros = MAX(0, exp * DEC_DIGITS_PER_DIGIT);
    n_dec_frac_digits = MAX(0, -exp) * DEC_DIGITS_PER_DIGIT;
    if (n_dec_frac_digits > dec_prec)
        d_adjust = n_dec_frac_digits - dec_prec;
    else
        d_adjust = 0;
    n_dec_fill_zeros = MAX(0, -exp - n_frac_digits) * DEC_DIGITS_PER_DIGIT;
    if (no_trailing_zeros)
        n_dec_trailing_frac_zeros = 0;
    else
        n_dec_trailing_frac_zeros = MAX(0, dec_prec - n_dec_frac_digits);
    max_n_chars =
        // zeros to be inserted after least significant digit and after
        // radix point
        n_dec_trailing_frac_zeros +
        // fractional digits in coeff
        n_dec_frac_digits - d_adjust +
        // zeros to inserted between fractional digits and radix point
        n_dec_fill_zeros +
        // zeros to be inserted after least significant digit and before
        // radix point
        n_dec_trailing_int_zeros +
        // maxinum integral digits in coeff
        n_int_digits * DEC_DIGITS_PER_DIGIT +
        // provision for sign, radix point and leading zero
        3;
    buf = (char *)fpdec_mem_alloc(max_n_chars + 1, 1);
    if (buf == NULL) MEMERROR_RETVAL(NULL)
    ch = buf;

    if (sign == FPDEC_SIGN_NEG)
        *(ch++) = '-';
    // atleast one integral digit
    if (n_int_digits == 0)
        *(ch++) = '0';
    else {
        ch = fill_in_leading_digit(ch, *digit);
        digit--;
        for (fpdec_n_digits_t i = 1; i < n_int_digits; i++)
            ch = fill_in_digit(ch, *(digit--), DEC_DIGITS_PER_DIGIT);
        ch = fill_in_zeros(ch, n_dec_trailing_int_zeros);
    }
    // radix point (only if there are any fractional digits)
    if (n_frac_digits != 0)
        *(ch++) = '.';
    // zeros between radix point and fractional digits
    ch = fill_in_zeros(ch, n_dec_fill_zeros);
    // full fractional digits
    for (fpdec_n_digits_t i = 1; i < n_frac_digits; i++) {
        ch = fill_in_digit(ch, *(digit--), DEC_DIGITS_PER_DIGIT);
    }
    // least significant fractional digit
    if (n_frac_digits > 0) {
        int n = DEC_DIGITS_PER_DIGIT - d_adjust;
        fpdec_digit_t adj_digit = *digit / _10_POW_N(d_adjust);
        if (no_trailing_zeros)
            while ((adj_digit % 10) == 0 && n-- > 0)
                adj_digit /= 10;
        ch = fill_in_digit(ch, adj_digit, n);
    }
    // trailing fractional zeros
    ch = fill_in_zeros(ch, n_dec_trailing_frac_zeros);

    assert(*ch == 0);
    return buf;
}

static inline char *
fill_in_u128(char *buf, uint128_t ui) {
    char dec_digits[MAX_N_DEC_DIGITS_IN_SHINT];
    char *ch = dec_digits;
    while (ui.lo != 0 || ui.hi != 0)
        *(ch++) = '0' + u128_idiv_10(&ui);
    while (--ch >= dec_digits)
        *(buf++) = *ch;
    return buf;
}

static char *
fpdec_shint_as_ascii_literal(const fpdec_t *fpdec,
                             const bool no_trailing_zeros) {
    char *buf;

    if (FPDEC_EQ_ZERO(fpdec)) {
        if (no_trailing_zeros || FPDEC_DEC_PREC(fpdec) == 0) {
            buf = fpdec_mem_alloc(2, 1);
            if (buf == NULL) MEMERROR_RETVAL(NULL)
            *buf = '0';
        }
        else {
            buf = fpdec_mem_alloc(FPDEC_DEC_PREC(fpdec) + 3, 1);
            if (buf == NULL) MEMERROR_RETVAL(NULL)
            buf[0] = '0';
            buf[1] = '.';
            for (int i = 2; i < FPDEC_DEC_PREC(fpdec) + 2; i++)
                buf[i] = '0';
        }
    }
    else {
        uint128_t t = U128_FROM_SHINT(fpdec);
        uint64_t r = 0;
        char *ch;

        buf = fpdec_mem_alloc(MAX_N_DEC_DIGITS_IN_SHINT + 3, 1);
        if (buf == NULL) MEMERROR_RETVAL(NULL)
        ch = buf;

        if (FPDEC_DEC_PREC(fpdec) > 0)
            // split shifted int
            r = u128_idiv_u64(&t, _10_POW_N(FPDEC_DEC_PREC(fpdec)));
        if (FPDEC_SIGN(fpdec) == FPDEC_SIGN_NEG)
            *(ch++) = '-';
        // atleast one integral digit
        if (t.lo == 0 && t.hi == 0)
            *(ch++) = '0';
        else
            ch = fill_in_u128(ch, t);
        // fractional part
        int n = FPDEC_DEC_PREC(fpdec);
        if (n > 0) {
            if (no_trailing_zeros) {
                if (r == 0)
                    n = 0;
                else
                    while (r > 0 && (r % 10) == 0) {
                        r /= 10;
                        n--;
                    }
            }
            // radix point (only if there are any fractional digits)
            if (n > 0) {
                *(ch++) = '.';
            }
            ch = fill_in_digit(ch, (fpdec_digit_t)r, n);
        }
        assert(*ch == 0);
    }
    return buf;
}

typedef char *(*v_as_ascii_literal)(const fpdec_t *, const bool);

const v_as_ascii_literal vtab_as_ascii_literal[2] = {
    fpdec_shint_as_ascii_literal,
    fpdec_dyn_as_ascii_literal,
};

char *
fpdec_as_ascii_literal(const fpdec_t *fpdec,
                       const bool no_trailing_zeros) {
    return DISPATCH_FUNC_VA(vtab_as_ascii_literal, fpdec, no_trailing_zeros);
}

int
fpdec_as_sign_coeff128_exp(fpdec_sign_t *sign, uint128_t *coeff, int64_t *exp,
                           const fpdec_t *fpdec) {
    *sign = FPDEC_SIGN(fpdec);
    if (*sign == 0) {
        coeff->hi = 0ULL;
        coeff->lo = 0ULL;
        *exp = 0ULL;
        return 0;
    }
    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        fpdec_n_digits_t n = FPDEC_DYN_N_DIGITS(fpdec);
        fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
        uint128_t t1, t2;
        int ntz, nsd;

        *exp = FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT;
        switch (n) {
            case 1:
                coeff->lo = digits[0];
                coeff->hi = 0UL;
                break;
            case 2:
                coeff->lo = digits[1];
                coeff->hi = 0UL;
                u128_imul_10_pow_n(coeff, DEC_DIGITS_PER_DIGIT);
                u128_iadd_u64(coeff, digits[0]);
                break;
            case 3:
                // try to fit normalized coeff into uint128
                t1.lo = digits[0];
                t1.hi = 0ULL;
                ntz = u128_eliminate_trailing_zeros(&t1,
                                                    DEC_DIGITS_PER_DIGIT);
                nsd = DEC_DIGITS_PER_DIGIT - ntz;
                coeff->lo = t1.lo;
                coeff->hi = 0ULL;
                t1.lo = digits[1];
                t1.hi = 0ULL;
                u128_imul_10_pow_n(&t1, nsd);
                u128_iadd_u128(coeff, &t1);
                t1.lo = digits[2];
                t1.hi = 0ULL;
                u128_imul_u64(&t1, RADIX);
                t2.lo = t1.lo;
                t2.hi = t1.hi;
                u128_imul_10_pow_n(&t2, nsd);
                if (u128_cmp(&t2, &t1) < 0)
                    // overflow
                    return -1;
                u128_iadd_u128(coeff, &t2);
                if (u128_cmp(coeff, &t2) < 0)
                    // overflow
                    return -1;
                *exp += ntz;
                return 0;
            default:
                // coeff has atleast 40 significant decimal digits
                return -1;
        }
    }
    else {
        coeff->hi = fpdec->hi;
        coeff->lo = fpdec->lo;
        *exp = -FPDEC_DEC_PREC(fpdec);
    }
    // normalize coeff
    *exp += u128_eliminate_trailing_zeros(coeff, MAX_N_DEC_DIGITS_IN_SHINT);
    return 0;
}

// Basic arithmetic operations

static inline fpdec_dec_prec_t
make_adjusted_shints(uint128_t *x_shint, uint128_t *y_shint,
                     const fpdec_dec_prec_t x_dec_prec,
                     const fpdec_dec_prec_t y_dec_prec) {
    int shift = x_dec_prec - y_dec_prec;
    fpdec_dec_prec_t prec;

    if (shift == 0)
        prec = x_dec_prec;
    else if (shift > 0) {
        prec = x_dec_prec;
        u128_imul_10_pow_n(y_shint, shift);
    }
    else {
        prec = y_dec_prec;
        u128_idecshift(x_shint, FPDEC_SIGN_POS, -shift, FPDEC_ROUND_DEFAULT);
    }
    return prec;
}

static error_t
fpdec_add_abs_shint_to_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    uint128_t x_shint = U128_FROM_SHINT(x);
    uint128_t y_shint = U128_FROM_SHINT(y);

    FPDEC_DEC_PREC(z) = make_adjusted_shints(&x_shint, &y_shint,
                                             FPDEC_DEC_PREC(x),
                                             FPDEC_DEC_PREC(y));
    u128_iadd_u128(&x_shint, &y_shint);
    z->lo = x_shint.lo;
    z->hi = x_shint.hi;
    if (U128_FITS_SHINT(x_shint))
        return FPDEC_OK;
    else
        return fpdec_shint_to_dyn(z);
}

static error_t
fpdec_add_abs_dyn_to_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    fpdec_n_digits_t n_shift, n_add_zeros;
    fpdec_digit_array_t *z_digits;
    const fpdec_digit_array_t *s_digits;
    fpdec_exp_t z_exp;

    if (FPDEC_DYN_EXP(x) == FPDEC_DYN_EXP(y)) {
        n_shift = 0;                // no need to adjust exponents
        n_add_zeros = 1;            // provision for potential carry-over
        if (FPDEC_DYN_N_DIGITS(x) >= FPDEC_DYN_N_DIGITS(y)) {
            z_digits = digits_copy(x->digit_array, n_shift, n_add_zeros);
            if (z_digits == NULL) MEMERROR
            s_digits = y->digit_array;
        }
        else {
            z_digits = digits_copy(y->digit_array, n_shift, n_add_zeros);
            if (z_digits == NULL) MEMERROR
            s_digits = x->digit_array;
        }
        z_exp = FPDEC_DYN_EXP(x);
    }
    else if (FPDEC_DYN_EXP(x) > FPDEC_DYN_EXP(y)) {
        n_shift = FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y);
        n_add_zeros = MAX((int)FPDEC_DYN_N_DIGITS(y) -
                          (int)FPDEC_DYN_N_DIGITS(x) -
                          (int)n_shift + 1,
                          1);
        z_digits = digits_copy(x->digit_array, n_shift, n_add_zeros);
        if (z_digits == NULL) MEMERROR
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(y);
    }
    else {
        n_shift = FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x);
        n_add_zeros = MAX((int)FPDEC_DYN_N_DIGITS(x) -
                          (int)FPDEC_DYN_N_DIGITS(y) -
                          (int)n_shift + 1,
                          1);
        z_digits = digits_copy(y->digit_array, n_shift, n_add_zeros);
        if (z_digits == NULL) MEMERROR
        s_digits = x->digit_array;
        z_exp = FPDEC_DYN_EXP(x);
    }
    digits_iadd_digits(z_digits, s_digits);
    z->exp = z_exp + digits_eliminate_trailing_zeros(z_digits);
    z->digit_array = z_digits;
    z->dyn_alloc = true;
    z->normalized = true;
    return FPDEC_OK;
}

static error_t
fpdec_add_abs_dyn_to_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t s;

    rc = fpdec_copy_shint_as_dyn(&s, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_add_abs_dyn_to_dyn(z, &s, y);
        fpdec_reset_to_zero(&s, 0);
    }
    return rc;
}

static error_t
fpdec_add_abs_shint_to_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t s;

    rc = fpdec_copy_shint_as_dyn(&s, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_add_abs_dyn_to_dyn(z, x, &s);
        fpdec_reset_to_zero(&s, 0);
    }
    return rc;
}

typedef error_t (*v_math_op)(fpdec_t *, const fpdec_t *, const fpdec_t *);

const v_math_op vtab_add_abs[4] = {
    fpdec_add_abs_shint_to_shint,
    fpdec_add_abs_dyn_to_shint,
    fpdec_add_abs_shint_to_dyn,
    fpdec_add_abs_dyn_to_dyn
};

// pre-condition: x >= y
static error_t
fpdec_sub_abs_shint_from_shint(fpdec_t *z, const fpdec_t *x,
                               const fpdec_t *y) {
    uint128_t x_shint = U128_FROM_SHINT(x);
    uint128_t y_shint = U128_FROM_SHINT(y);

    FPDEC_DEC_PREC(z) = make_adjusted_shints(&x_shint, &y_shint,
                                             FPDEC_DEC_PREC(x),
                                             FPDEC_DEC_PREC(y));
    u128_isub_u128(&x_shint, &y_shint);
    z->lo = x_shint.lo;
    z->hi = x_shint.hi;
    return FPDEC_OK;
}

static error_t
fpdec_sub_abs_dyn_from_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    fpdec_n_digits_t n_shift;
    fpdec_digit_array_t *z_digits;
    fpdec_digit_array_t *s_digits;
    bool s_digits_is_copy = false;
    fpdec_exp_t z_exp;

    if (FPDEC_DYN_EXP(x) == FPDEC_DYN_EXP(y)) {
        n_shift = 0;                // no need to adjust exponents
        z_digits = digits_copy(x->digit_array, n_shift, 0);
        if (z_digits == NULL) MEMERROR
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(x);
    }
    else if (FPDEC_DYN_EXP(x) > FPDEC_DYN_EXP(y)) {
        n_shift = FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y);
        z_digits = digits_copy(x->digit_array, n_shift, 0);
        if (z_digits == NULL) MEMERROR
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(y);
    }
    else {
        z_digits = digits_copy(x->digit_array, 0, 0);
        if (z_digits == NULL) MEMERROR
        n_shift = FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x);
        s_digits = digits_copy(y->digit_array, n_shift, 0);
        if (s_digits == NULL) MEMERROR
        s_digits_is_copy = true;
        z_exp = FPDEC_DYN_EXP(x);
    }
    digits_isub_digits(z_digits, s_digits);
    if (s_digits_is_copy)
        fpdec_mem_free((void *)s_digits);
    z->exp = z_exp;
    z->digit_array = z_digits;
    z->dyn_alloc = true;
    fpdec_dyn_normalize(z);
    return FPDEC_OK;
}

static error_t
fpdec_sub_abs_dyn_from_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t s;

    rc = fpdec_copy_shint_as_dyn(&s, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_sub_abs_dyn_from_dyn(z, &s, y);
        fpdec_reset_to_zero(&s, 0);
    }
    return rc;
}

static error_t
fpdec_sub_abs_shint_from_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t s;

    rc = fpdec_copy_shint_as_dyn(&s, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_sub_abs_dyn_from_dyn(z, x, &s);
        fpdec_reset_to_zero(&s, 0);
    }
    return rc;
}

const v_math_op vtab_sub_abs[4] = {
    fpdec_sub_abs_shint_from_shint,
    fpdec_sub_abs_dyn_from_shint,
    fpdec_sub_abs_shint_from_dyn,
    fpdec_sub_abs_dyn_from_dyn
};

error_t
fpdec_add(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    int cmp;

    ASSERT_FPDEC_IS_ZEROED(z);

    if (FPDEC_EQ_ZERO(x))
        return fpdec_copy(z, y);
    if (FPDEC_EQ_ZERO(y))
        return fpdec_copy(z, x);

    FPDEC_DEC_PREC(z) = MAX(FPDEC_DEC_PREC(x), FPDEC_DEC_PREC(y));
    if (FPDEC_SIGN(x) == FPDEC_SIGN(y)) {
        // same sign => x + y = sign(x) (|x| + |y|)
        FPDEC_SIGN(z) = FPDEC_SIGN(x);
        return DISPATCH_BIN_OP(vtab_add_abs, z, x, y);
    }
    // sign(x) != sign(y) ...
    cmp = fpdec_compare(x, y, true);
    if (cmp == 1) {
        // ... and |x| > |y| => x + y = sign(x) (|x| - |y|)
        FPDEC_SIGN(z) = FPDEC_SIGN(x);
        return DISPATCH_BIN_OP(vtab_sub_abs, z, x, y);
    }
    if (cmp == -1) {
        // ... and |x| < |y| => x + y = sign(y) (|y| - |x|)
        FPDEC_SIGN(z) = FPDEC_SIGN(y);
        return DISPATCH_BIN_OP(vtab_sub_abs, z, y, x);
    }
    // ... and |x| = |y| => x + y = 0
    return FPDEC_OK;
}

error_t
fpdec_sub(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    int cmp;

    ASSERT_FPDEC_IS_ZEROED(z);

    if (FPDEC_EQ_ZERO(x))
        return fpdec_neg(z, y);
    if (FPDEC_EQ_ZERO(y))
        return fpdec_copy(z, x);

    FPDEC_DEC_PREC(z) = MAX(FPDEC_DEC_PREC(x), FPDEC_DEC_PREC(y));
    if (FPDEC_SIGN(x) != FPDEC_SIGN(y)) {
        // sign(x) != sign(y) => x - y = sign(x) (|x| + |y|)
        FPDEC_SIGN(z) = FPDEC_SIGN(x);
        return DISPATCH_BIN_OP(vtab_add_abs, z, x, y);
    }
    // same sign ...
    cmp = fpdec_compare(x, y, true);
    if (cmp == 1) {
        // ... and |x| > |y| => x - y = sign(x) (|x| - |y|)
        FPDEC_SIGN(z) = FPDEC_SIGN(x);
        return DISPATCH_BIN_OP(vtab_sub_abs, z, x, y);
    }
    if (cmp == -1) {
        // ... and |x| < |y| => x - y = ~sign(y) (|y| - |x|)
        FPDEC_SIGN(z) = FPDEC_SIGN(y) * -1;
        return DISPATCH_BIN_OP(vtab_sub_abs, z, y, x);
    }
    // ... and |x| = |y| => x - y = 0
    return FPDEC_OK;

}

static error_t
fpdec_mul_abs_dyn_by_u64(fpdec_t *z, const fpdec_t *x, const uint64_t y) {
    fpdec_digit_array_t *z_digits = digits_copy(x->digit_array, 0, 1);

    if (z_digits == NULL) MEMERROR

    digits_imul_digit(z_digits, y);
    z->digit_array = z_digits;
    FPDEC_IS_DYN_ALLOC(z) = true;
    return FPDEC_OK;
}

static error_t
fpdec_mul_abs_shint_by_u64(fpdec_t *z, const fpdec_t *x, const uint64_t y) {
    uint128_t z_shint = U128_FROM_SHINT(x);

    u128_imul_u64(&z_shint, y);
    if (U128_FITS_SHINT(z_shint)) {
        z->lo = z_shint.lo;
        z->hi = z_shint.hi;
        return FPDEC_OK;
    }
    return FPDEC_N_DIGITS_LIMIT_EXCEEDED;
}

static error_t
fpdec_mul_abs_dyn_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    fpdec_digit_array_t *z_digits;
    int64_t exp;

    z_digits = digits_mul(x->digit_array, y->digit_array);
    if (z_digits == NULL) MEMERROR
    z->digit_array = z_digits;
    exp = (int64_t)FPDEC_DYN_EXP(x) + (int64_t)FPDEC_DYN_EXP(y);
    if (exp > FPDEC_MAX_EXP || exp < INT32_MIN) ERROR(
        FPDEC_EXP_LIMIT_EXCEEDED)
    z->exp = (fpdec_exp_t)exp;
    FPDEC_IS_DYN_ALLOC(z) = true;
    return FPDEC_OK;
}

static error_t
fpdec_mul_abs_shint_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t x_dyn;

    rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_mul_abs_dyn_by_dyn(z, &x_dyn, y);
        fpdec_reset_to_zero(&x_dyn, 0);
    }
    return rc;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ArgumentSelectionDefects"

static error_t
fpdec_mul_abs_dyn_by_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    return fpdec_mul_abs_shint_by_dyn(z, y, x);
}

#pragma clang diagnostic pop

static error_t
fpdec_mul_abs_shint_by_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    fpdec_t y_dyn;

    if (x->hi == 0 && fpdec_mul_abs_shint_by_u64(z, y, x->lo) == FPDEC_OK)
        return FPDEC_OK;
    if (y->hi == 0 && fpdec_mul_abs_shint_by_u64(z, x, y->lo) == FPDEC_OK)
        return FPDEC_OK;

    rc = fpdec_copy_shint_as_dyn(&y_dyn, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_mul_abs_shint_by_dyn(z, x, &y_dyn);
        fpdec_reset_to_zero(&y_dyn, 0);
    }
    return rc;
}

const v_math_op vtab_mul_abs[4] = {
    fpdec_mul_abs_shint_by_shint,
    fpdec_mul_abs_shint_by_dyn,
    fpdec_mul_abs_dyn_by_shint,
    fpdec_mul_abs_dyn_by_dyn
};

error_t
fpdec_mul(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(z);

    if (FPDEC_EQ_ZERO(x) || FPDEC_EQ_ZERO(y))
        return FPDEC_OK;

    FPDEC_SIGN(z) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
    FPDEC_DEC_PREC(z) = FPDEC_DEC_PREC(x) + FPDEC_DEC_PREC(y);
    if (FPDEC_DEC_PREC(z) < FPDEC_DEC_PREC(x)) ERROR(
        FPDEC_PREC_LIMIT_EXCEEDED)
    if (FPDEC_DEC_PREC(z) <= MAX_DEC_PREC_FOR_SHINT ||
        FPDEC_IS_DYN_ALLOC(x) || FPDEC_IS_DYN_ALLOC(y)) {
        rc = DISPATCH_BIN_OP(vtab_mul_abs, z, x, y);
    }
    else {
        // force result to dyn variant
        fpdec_t x_dyn;
        rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
        if (rc == FPDEC_OK) {
            rc = DISPATCH_BIN_OP(vtab_mul_abs, z, &x_dyn, y);
            fpdec_reset_to_zero(&x_dyn, 0);
        }
    }
    if (FPDEC_IS_DYN_ALLOC(z))
        fpdec_dyn_normalize(z);
    return rc;
}

error_t
fpdec_divmod_abs_dyn_by_dyn(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                            const fpdec_t *y, bool neg_quot) {
    const fpdec_n_digits_t n_shift_x =
        MAX(0, FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y));
    const fpdec_n_digits_t n_shift_y =
        MAX(0, FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x));
    fpdec_digit_array_t *q_digits, *r_digits;

    if (FPDEC_DYN_N_DIGITS(y) == 1 && n_shift_y == 0) {
        q_digits = digits_div_digit(x->digit_array, n_shift_x,
                                    FPDEC_DYN_DIGITS(y)[0], &r->lo);
        if (q_digits == NULL) MEMERROR
        // adjust negativ quotient?
        if (neg_quot && r->lo != 0) {
            // Because there is a remainder,
            // q_digits < x->digits * RADIX ^ n_shift_x.
            // x->digits <= RADIX ^ x->n_signif - 1, so
            // q_digits < RADIX ^ (x->n_signif + n_shift_x) - 1.
            // That means we can safely increment q_digits.
            digits_iadd_digit(q_digits, 1);
            r->lo = FPDEC_DYN_DIGITS(y)[0] - r->lo;
        }
    }
    else {
        q_digits = digits_divmod(x->digit_array, n_shift_x, y->digit_array,
                                 n_shift_y, &r_digits);
        if (q_digits == NULL) MEMERROR
        if (r_digits == NULL) MEMERROR
        r->digit_array = r_digits;
        r->dyn_alloc = true;
        FPDEC_DYN_EXP(r) = MIN(FPDEC_DYN_EXP(y), FPDEC_DYN_EXP(x));
        // adjust negativ quotient?
        if (neg_quot && !digits_all_zero(r_digits->digits,
                                         r_digits->n_signif)) {
            error_t rc;
            fpdec_t t = *r;
            *r = FPDEC_ZERO;
            fpdec_dyn_normalize(&t);
            rc = fpdec_sub(r, y, &t);
            if (rc != FPDEC_OK)
                return rc;
            // Safe to increment q_digits. See comment above.
            digits_iadd_digit(q_digits, 1);
        }
    }
    q->digit_array = q_digits;
    q->dyn_alloc = true;
    FPDEC_DYN_EXP(q) = 0;
    return FPDEC_OK;
}

error_t
fpdec_divmod_abs_dyn_by_shint(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                              const fpdec_t *y, bool neg_quot) {
    error_t rc;
    fpdec_t y_dyn;

    rc = fpdec_copy_shint_as_dyn(&y_dyn, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_divmod_abs_dyn_by_dyn(q, r, x, &y_dyn, neg_quot);
        fpdec_reset_to_zero(&y_dyn, 0);
    }
    return rc;
}

error_t
fpdec_divmod_abs_shint_by_dyn(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                              const fpdec_t *y, bool neg_quot) {
    error_t rc;
    fpdec_t x_dyn;

    rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_divmod_abs_dyn_by_dyn(q, r, &x_dyn, y, neg_quot);
        fpdec_reset_to_zero(&x_dyn, 0);
    }
    return rc;
}

error_t
fpdec_divmod_abs_shint_by_shint(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                                const fpdec_t *y, bool neg_quot) {
    uint128_t q_shint = U128_FROM_SHINT(x);
    uint128_t y_shint = U128_FROM_SHINT(y);
    uint128_t r_shint;

    FPDEC_DEC_PREC(r) = make_adjusted_shints(&q_shint, &y_shint,
                                             FPDEC_DEC_PREC(x),
                                             FPDEC_DEC_PREC(y));
    if (y_shint.hi == 0) {
        r_shint.hi = 0;
        r_shint.lo = u128_idiv_u64(&q_shint, y_shint.lo);
    }
    else
        u128_idiv_u128(&r_shint, &q_shint, &y_shint);
    // adjust negativ quotient?
    if (neg_quot && U128_NE_ZERO(r_shint)) {
        u128_incr(&q_shint);
        if (r_shint.hi == 0)
            u128_isub_u64(&y_shint, r_shint.lo);
        else
            u128_isub_u128(&y_shint, &r_shint);
        r_shint = y_shint;
    }
    if (U128_FITS_SHINT(q_shint)) {
        q->lo = q_shint.lo;
        q->hi = q_shint.hi;
        r->lo = r_shint.lo;
        r->hi = r_shint.hi;
        return FPDEC_OK;
    }
    else {
        error_t rc;
        fpdec_t x_dyn;
        rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
        if (rc == FPDEC_OK) {
            rc = fpdec_divmod_abs_dyn_by_shint(q, r, &x_dyn, y, neg_quot);
            fpdec_reset_to_zero(&x_dyn, 0);
        }
        return rc;
    }
}

typedef error_t (*v_divmod_op)(fpdec_t *, fpdec_t *, const fpdec_t *,
                               const fpdec_t *, bool);

const v_divmod_op vtab_divmod_abs[4] = {
    fpdec_divmod_abs_shint_by_shint,
    fpdec_divmod_abs_shint_by_dyn,
    fpdec_divmod_abs_dyn_by_shint,
    fpdec_divmod_abs_dyn_by_dyn
};

error_t
fpdec_divmod(fpdec_t *q, fpdec_t *r, const fpdec_t *x, const fpdec_t *y) {
    error_t rc;
    int cmp;

    ASSERT_FPDEC_IS_ZEROED(q);
    ASSERT_FPDEC_IS_ZEROED(r);

    if (FPDEC_EQ_ZERO(y)) ERROR(FPDEC_DIVIDE_BY_ZERO)

    if (FPDEC_EQ_ZERO(x))
        return FPDEC_OK;

    cmp = fpdec_compare(x, y, true);
    if (cmp < 0) {                                  // x < y
        if (FPDEC_SIGN(x) == FPDEC_SIGN(y))
            return fpdec_copy(r, x);
        else {
            fpdec_copy(q, &FPDEC_MINUS_ONE);
            return fpdec_add(r, x, y);
        }
    }
    if (cmp == 0) {                                 // x = y
        if (FPDEC_SIGN(x) == FPDEC_SIGN(y))
            return fpdec_copy(q, &FPDEC_ONE);
        else
            return fpdec_copy(q, &FPDEC_MINUS_ONE);
    }

    // x > y
    FPDEC_SIGN(q) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
    FPDEC_SIGN(r) = FPDEC_SIGN(y);
    FPDEC_DEC_PREC(r) = MAX(FPDEC_DEC_PREC(x), FPDEC_DEC_PREC(y));
    rc = vtab_divmod_abs[((FPDEC_IS_DYN_ALLOC(x)) << 1U) +
                         FPDEC_IS_DYN_ALLOC(y)](q, r, x, y, FPDEC_LT_ZERO(q));

    if (FPDEC_IS_DYN_ALLOC(q))
        fpdec_dyn_normalize(q);
    if (FPDEC_IS_DYN_ALLOC(r))
        fpdec_dyn_normalize(r);
    else if (r->lo == 0 && r->hi == 0)
        FPDEC_SIGN(r) = 0;
    return rc;
}

error_t
fpdec_div_abs_dyn_by_dyn_exact(fpdec_t *z, const fpdec_t *x,
                               const fpdec_t *y) {
    fpdec_digit_array_t *q_digits;
    int exp = FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y);

    if (exp < FPDEC_MIN_EXP) ERROR(FPDEC_PREC_LIMIT_EXCEEDED)

    q_digits = digits_div_max_prec(x->digit_array, y->digit_array,
                                   &exp);
    if (q_digits == NULL) MEMERROR
    if (exp > FPDEC_MAX_EXP) ERROR(FPDEC_EXP_LIMIT_EXCEEDED)
    if (exp <= FPDEC_MIN_EXP) ERROR(FPDEC_PREC_LIMIT_EXCEEDED)
    FPDEC_DYN_EXP(z) = exp;
    // calculate decimal precision
    FPDEC_DEC_PREC(z) = MAX(0, -exp * DEC_DIGITS_PER_DIGIT);
    if (FPDEC_DEC_PREC(z) > 0) {
        for (fpdec_digit_t *d = q_digits->digits;; ++d) {
            if (*d == 0)
                FPDEC_DEC_PREC(z) -= DEC_DIGITS_PER_DIGIT;
            else {
                fpdec_digit_t t = *d;
                while (t % 10 == 0) {
                    t /= 10;
                    FPDEC_DEC_PREC(z)--;
                }
                break;
            }
        }
    }
    z->digit_array = q_digits;
    z->dyn_alloc = true;
    return FPDEC_OK;
}

error_t
fpdec_div_abs_dyn_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                         const int prec_limit,
                         const enum FPDEC_ROUNDING_MODE rounding) {
    if (prec_limit == -1)
        return fpdec_div_abs_dyn_by_dyn_exact(z, x, y);

    else {
        FPDEC_DEC_PREC(z) = prec_limit;
        // calculate digit shifts to accomplish wanted precision
        int quot_exp = FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y);
        int res_exp = -prec_limit / DEC_DIGITS_PER_DIGIT - 1;
        fpdec_n_digits_t x_n_shift, y_n_shift;
        fpdec_n_digits_t x_n_digits_after_shift, y_n_digits_after_shift;

        if (res_exp < quot_exp) {
            x_n_shift = (fpdec_n_digits_t)(quot_exp - res_exp);
            x_n_digits_after_shift = FPDEC_DYN_N_DIGITS(x) + x_n_shift;
            y_n_shift = 0;
            y_n_digits_after_shift = FPDEC_DYN_N_DIGITS(y);
        }
        else {
            x_n_shift = 0;
            x_n_digits_after_shift = FPDEC_DYN_N_DIGITS(x);
            y_n_shift = (fpdec_n_digits_t)(res_exp - quot_exp);
            y_n_digits_after_shift = FPDEC_DYN_N_DIGITS(y) + y_n_shift;
        }
        // check if result will have significant digits
        if (x_n_digits_after_shift >= y_n_digits_after_shift) {
            fpdec_digit_array_t *q_digits =
                digits_div_limit_prec(x->digit_array, x_n_shift,
                                      y->digit_array, y_n_shift);
            if (q_digits == NULL) MEMERROR
            int d_shift = prec_limit % DEC_DIGITS_PER_DIGIT;
            digits_round(q_digits, FPDEC_SIGN(z),
                         DEC_DIGITS_PER_DIGIT - d_shift,
                         rounding);
            FPDEC_DYN_EXP(z) = res_exp;
            z->digit_array = q_digits;
            z->dyn_alloc = true;
        }
    }
    return FPDEC_OK;
}

error_t
fpdec_div_abs_dyn_by_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                           const int prec_limit,
                           const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    fpdec_t y_dyn;

    rc = fpdec_copy_shint_as_dyn(&y_dyn, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_div_abs_dyn_by_dyn(z, x, &y_dyn, prec_limit, rounding);
        fpdec_reset_to_zero(&y_dyn, 0);
    }
    return rc;
}

error_t
fpdec_div_abs_shint_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                           const int prec_limit,
                           const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    fpdec_t x_dyn;

    rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_div_abs_dyn_by_dyn(z, &x_dyn, y, prec_limit, rounding);
        fpdec_reset_to_zero(&x_dyn, 0);
    }
    return rc;
}

error_t
fpdec_div_abs_shint_by_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                             const int prec_limit,
                             const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    uint128_t divident = U128_FROM_SHINT(x);
    uint128_t divisor = U128_FROM_SHINT(y);
    uint128_t rem = {0, 0};
    int shift;
    unsigned n_trailing_zeros;

    if (prec_limit == -1)
        shift = MAX_DEC_PREC_FOR_SHINT - FPDEC_DEC_PREC(x) +
                FPDEC_DEC_PREC(y);
    else
        shift = MIN(prec_limit, MAX_DEC_PREC_FOR_SHINT) - FPDEC_DEC_PREC(x) +
                FPDEC_DEC_PREC(y);
    if (shift > 0)
        u128_imul_10_pow_n(&divident, shift);
    else if (shift < 0)
        u128_imul_10_pow_n(&divisor, -shift);
    if (divisor.hi == 0)
        rem.lo = u128_idiv_u64(&divident, divisor.lo);
    else
        u128_idiv_u128(&rem, &divident, &divisor);
    if (U128_NE_ZERO(rem)) {
        if (prec_limit == -1 || prec_limit > MAX_DEC_PREC_FOR_SHINT) {
            // result is not exact enough
            fpdec_t x_dyn;
            rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
            if (rc == FPDEC_OK) {
                rc = fpdec_div_abs_dyn_by_shint(z, &x_dyn, y, prec_limit,
                                                rounding);
                fpdec_reset_to_zero(&x_dyn, 0);
            }
            return rc;
        }
        else {
            // result (in divident) truncated to prec_limit, check rounding
            if (round_u128(FPDEC_SIGN(z), &divident, &rem, &divisor,
                           rounding))
                u128_incr(&divident);
            FPDEC_DEC_PREC(z) = prec_limit;
        }
    }
    else {
        if (prec_limit == -1 && shift > 0) {
            n_trailing_zeros = u128_eliminate_trailing_zeros(&divident,
                                                             shift);
            FPDEC_DEC_PREC(z) = MAX_DEC_PREC_FOR_SHINT - n_trailing_zeros;
        }
        else if (prec_limit > MAX_DEC_PREC_FOR_SHINT) {
            FPDEC_DEC_PREC(z) = MAX_DEC_PREC_FOR_SHINT;
            z->lo = divident.lo;
            z->hi = divident.hi;
            fpdec_shint_to_dyn(z);
            FPDEC_DEC_PREC(z) = prec_limit;
            return FPDEC_OK;
        }
        else
            FPDEC_DEC_PREC(z) = prec_limit;
    }
    z->lo = divident.lo;
    z->hi = divident.hi;
    return FPDEC_OK;
}

typedef error_t (*v_div_op)(fpdec_t *, const fpdec_t *, const fpdec_t *,
                            const int, const enum FPDEC_ROUNDING_MODE);

const v_div_op vtab_div_abs[4] = {
    fpdec_div_abs_shint_by_shint,
    fpdec_div_abs_shint_by_dyn,
    fpdec_div_abs_dyn_by_shint,
    fpdec_div_abs_dyn_by_dyn
};

error_t
fpdec_div(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
          const int prec_limit, const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(z);
    assert(prec_limit >= -1);
    assert(prec_limit <= FPDEC_MAX_DEC_PREC);

    if (FPDEC_EQ_ZERO(y)) ERROR(FPDEC_DIVIDE_BY_ZERO)

    if (FPDEC_EQ_ZERO(x))
        return FPDEC_OK;

    rc = DISPATCH_BIN_OP_VA(vtab_div_abs, z, x, y, prec_limit, rounding);
    if (rc != FPDEC_OK)
        return rc;

    if (FPDEC_IS_DYN_ALLOC(z)) {
        FPDEC_SIGN(z) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
        fpdec_dyn_normalize(z);
    }
    else {
        if (z->lo == 0 && z->hi == 0)
            FPDEC_SIGN(z) = FPDEC_SIGN_ZERO;
        else
            FPDEC_SIGN(z) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
    }
    return FPDEC_OK;
}

// Deallocator

void
fpdec_reset_to_zero(fpdec_t *fpdec, fpdec_dec_prec_t dec_prec) {
    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        fpdec_mem_free((void *)fpdec->digit_array);
    }
    memset((void *)fpdec, 0, sizeof(fpdec_t));
    FPDEC_DEC_PREC(fpdec) = dec_prec;
}
