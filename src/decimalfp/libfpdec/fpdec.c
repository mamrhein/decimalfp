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

#include "fpdec.h"
#include "digit_array_.h"
#include "parser.h"
#include "shifted_int_.h"
#include "rounding_.h"

/*****************************************************************************
*  Macros
*****************************************************************************/

#define U128_FROM_SHINT(x) {x->lo, x->hi}

#define FPDEC_DYN_EXP(fpdec) (((fpdec_t*)fpdec)->exp)

#define FPDEC_DYN_N_DIGITS(fpdec) (((fpdec_t*)fpdec)->digit_array->n_signif)

#define FPDEC_DYN_DIGITS(fpdec) (((fpdec_t*)fpdec)->digit_array->digits)

#define FPDEC_DYN_MOST_SIGNIF_DIGIT(fpdec) \
        (FPDEC_DYN_DIGITS(fpdec)[FPDEC_DYN_N_DIGITS(fpdec) - 1])

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
        printf("exp: %d\n", FPDEC_EXP(fpdec));
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
        goto EXIT;
    fpdec->dyn_alloc = true;
    fpdec->exp += digits_eliminate_trailing_zeros(fpdec->digit_array);
    if (FPDEC_DYN_N_DIGITS(fpdec) > 0) {
        fpdec->normalized = true;
    }
    else {      // corner case: result == 0
        fpdec_reset_to_zero(fpdec, 0);
    }
    fpdec->dec_prec = MAX(0, -dec_repr->exp);
EXIT:
    if (dec_repr != &st_dec_repr) {
        fpdec_mem_free(dec_repr);
    }
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
    int rel_pos_radix_point = FPDEC_DYN_N_DIGITS(fpdec) + FPDEC_EXP(fpdec);
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
    fpdec_sign_t x_sign, y_sign;
    int x_magn, y_magn;

    if (ignore_sign) {
        if (FPDEC_SIGN(x) == 0)
            return FPDEC_SIGN(y) ? -1 : 0;
        if (FPDEC_SIGN(y) == 0)
            return FPDEC_SIGN(x) != 0;
        x_sign = FPDEC_SIGN_POS;
    }
    else {
        x_sign = FPDEC_SIGN(x);
        y_sign = FPDEC_SIGN(y);
        if (x_sign != y_sign)
            return CMP(x_sign, y_sign);
        if (x_sign == 0)
            return 0;
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
    if (dec_prec <= MAX_DEC_PREC_FOR_SHINT) {
        size_t n_dec_digits = MAX(fpdec_magnitude(fpdec), 0) + dec_prec;
        if (n_dec_digits <= MAX_N_DEC_DIGITS_IN_SHINT) {
            fpdec_sign_t sign = FPDEC_SIGN(fpdec);
            uint128_t shint = {0, 0};
            uint128_t f = {0, 0};
            fpdec_n_digits_t n_digits = FPDEC_DYN_N_DIGITS(fpdec);
            fpdec_n_digits_t digit_idx = 0;
            fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
            uint64_t dec_shift = _10_POW_N(dec_prec);
            switch FPDEC_EXP(fpdec) {
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

static error_t
fpdec_dyn_adjust_to_prec(fpdec_t *fpdec,
                         const fpdec_dec_prec_t dec_prec,
                         const enum FPDEC_ROUNDING_MODE rounding) {
    size_t radix_point_at = -FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT;

    if (dec_prec >= FPDEC_DEC_PREC(fpdec) || dec_prec >= radix_point_at) {
        // no need to adjust digits
        FPDEC_DEC_PREC(fpdec) = dec_prec;
    }
    else {
        // need to shorten / round digits
        size_t dec_shift = radix_point_at - dec_prec;
        if (dec_shift >
                FPDEC_DYN_N_DIGITS(fpdec) * DEC_DIGITS_PER_DIGIT) {
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
        FPDEC_DEC_PREC(fpdec) = dec_prec;
        fpdec_dyn_normalize(fpdec);
    }
    return FPDEC_OK;
}

static error_t
fpdec_shint_adjust_to_prec(fpdec_t *fpdec,
                           const fpdec_dec_prec_t dec_prec,
                           const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    int dec_shift = dec_prec - FPDEC_DEC_PREC(fpdec);
    uint128_t shifted = U128_FROM_SHINT(fpdec);

    u128_idecshift(&shifted, FPDEC_SIGN(fpdec), dec_shift, rounding);
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
    FPDEC_DEC_PREC(fpdec) = dec_prec;
    return FPDEC_OK;
}

typedef error_t (*v_adjust_to_prec)(fpdec_t *, fpdec_dec_prec_t,
                                    enum FPDEC_ROUNDING_MODE);

const v_adjust_to_prec vtab_adjust_to_prec[2] = {fpdec_shint_adjust_to_prec,
                                                 fpdec_dyn_adjust_to_prec};

error_t
fpdec_adjusted(fpdec_t *fpdec, const fpdec_t *src,
               const fpdec_dec_prec_t dec_prec,
               const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(fpdec);

    rc = fpdec_copy(fpdec, src);
    if (rc == ENOMEM) MEMERROR

    if (FPDEC_DEC_PREC(fpdec) == dec_prec)
        return FPDEC_OK;

    if (dec_prec > MAX_DEC_PREC_FOR_SHINT && !FPDEC_IS_DYN_ALLOC(fpdec)) {
        rc = fpdec_shint_to_dyn(fpdec);
        if (rc != FPDEC_OK)
            return rc;
    }

    return DISPATCH_FUNC_VA(vtab_adjust_to_prec, fpdec, dec_prec, rounding);
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
        n_add_zeros = MAX((int) FPDEC_DYN_N_DIGITS(y) -
                                  (int) FPDEC_DYN_N_DIGITS(x) -
                                  (int) n_shift + 1,
                          1);
        z_digits = digits_copy(x->digit_array, n_shift, n_add_zeros);
        if (z_digits == NULL) MEMERROR
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(y);
    }
    else {
        n_shift = FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x);
        n_add_zeros = MAX((int) FPDEC_DYN_N_DIGITS(x) -
                                  (int) FPDEC_DYN_N_DIGITS(y) -
                                  (int) n_shift + 1,
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
        fpdec_mem_free((void *) s_digits);
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

    z_digits = digits_mul(x->digit_array, y->digit_array);
    if (z_digits == NULL) MEMERROR
    z->digit_array = z_digits;
    z->exp = FPDEC_EXP(x) + FPDEC_EXP(y);
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
                            const fpdec_t *y) {
    const fpdec_n_digits_t n_shift_x =
            MAX(0, FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y));
    const fpdec_n_digits_t n_shift_y =
            MAX(0, FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x));
    fpdec_digit_array_t *q_digits, *r_digits;

    if (FPDEC_N_DIGITS(y) == 1 && n_shift_y == 0) {
        q_digits = digits_div_digit(x->digit_array, n_shift_x,
                                    FPDEC_DYN_DIGITS(y)[0], &r->lo);
        if (q_digits == NULL) MEMERROR
    }
    else {
        q_digits = digits_divmod(x->digit_array, n_shift_x, y->digit_array,
                                 n_shift_y, &r_digits);
        if (q_digits == NULL) MEMERROR
        if (r_digits == NULL) MEMERROR
        r->digit_array = r_digits;
        r->dyn_alloc = true;
        FPDEC_DYN_EXP(r) = MIN(FPDEC_DYN_EXP(y), FPDEC_DYN_EXP(x));
    }
    q->digit_array = q_digits;
    q->dyn_alloc = true;
    FPDEC_DYN_EXP(q) = 0;
    return FPDEC_OK;
}

error_t
fpdec_divmod_abs_dyn_by_shint(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                              const fpdec_t *y) {
    error_t rc;
    fpdec_t y_dyn;

    rc = fpdec_copy_shint_as_dyn(&y_dyn, y);
    if (rc == FPDEC_OK) {
        rc = fpdec_divmod_abs_dyn_by_dyn(q, r, x, &y_dyn);
        fpdec_reset_to_zero(&y_dyn, 0);
    }
    return rc;
}

error_t
fpdec_divmod_abs_shint_by_dyn(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                              const fpdec_t *y) {
    error_t rc;
    fpdec_t x_dyn;

    rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_divmod_abs_dyn_by_dyn(q, r, &x_dyn, y);
        fpdec_reset_to_zero(&x_dyn, 0);
    }
    return rc;
}

error_t
fpdec_divmod_abs_shint_by_shint(fpdec_t *q, fpdec_t *r, const fpdec_t *x,
                                const fpdec_t *y) {
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
            rc = fpdec_divmod_abs_dyn_by_shint(q, r, &x_dyn, y);
            fpdec_reset_to_zero(&x_dyn, 0);
        }
        return rc;
    }
}

typedef error_t (*v_divmod_op)(fpdec_t *, fpdec_t *, const fpdec_t *,
                               const fpdec_t *);

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
    FPDEC_SIGN(r) = FPDEC_SIGN(x);
    FPDEC_DEC_PREC(r) = MAX(FPDEC_DEC_PREC(x), FPDEC_DEC_PREC(y));
    rc = vtab_divmod_abs[((FPDEC_IS_DYN_ALLOC(x)) << 1U) +
            FPDEC_IS_DYN_ALLOC(y)](q, r, x, y);

    if (FPDEC_IS_DYN_ALLOC(q))
        fpdec_dyn_normalize(q);
    if (FPDEC_IS_DYN_ALLOC(r))
        fpdec_dyn_normalize(r);
    else if (r->lo == 0 && r->hi == 0)
        FPDEC_SIGN(r) = 0;
    return rc;
}

error_t
fpdec_div_abs_dyn_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                         const int prec_limit,
                         const enum FPDEC_ROUNDING_MODE rounding) {
    fpdec_digit_array_t *q_digits, *r_digits;
    int exp = FPDEC_EXP(x) - FPDEC_EXP(y);

    if (prec_limit == -1) {
        q_digits = digits_div_max_prec(x->digit_array, y->digit_array,
                                       &exp);
        if (q_digits == NULL) MEMERROR
        if (exp > FPDEC_MAX_EXP) ERROR(FPDEC_EXP_LIMIT_EXCEEDED)
        if (exp < FPDEC_MIN_EXP) ERROR(FPDEC_PREC_LIMIT_EXCEEDED)
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
    }
    else {
        int d_shift = prec_limit % DEC_DIGITS_PER_DIGIT;
        FPDEC_DYN_EXP(z) = -prec_limit / DEC_DIGITS_PER_DIGIT - 1;
        q_digits = digits_div_limit_prec(x->digit_array, y->digit_array,
                                         FPDEC_DYN_EXP(z) - exp);
        if (q_digits == NULL) MEMERROR
        digits_round(q_digits, FPDEC_SIGN(z), DEC_DIGITS_PER_DIGIT - d_shift,
                     rounding);
        FPDEC_DEC_PREC(z) = prec_limit;
    }
    z->digit_array = q_digits;
    z->dyn_alloc = true;
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
    unsigned n_trailing_zeros = 0;

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

    FPDEC_SIGN(z) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
    rc = DISPATCH_BIN_OP_VA(vtab_div_abs, z, x, y, prec_limit, rounding);

    if (FPDEC_IS_DYN_ALLOC(z))
        fpdec_dyn_normalize(z);
    return rc;
}

// Deallocator

void
fpdec_reset_to_zero(fpdec_t *fpdec, fpdec_dec_prec_t dec_prec) {
    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        fpdec_mem_free((void *) fpdec->digit_array);
    }
    memset((void *) fpdec, 0, sizeof(fpdec_t));
    FPDEC_DEC_PREC(fpdec) = dec_prec;
}
