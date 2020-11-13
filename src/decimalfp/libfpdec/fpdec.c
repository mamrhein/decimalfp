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
#include "format_spec.h"
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
const fpdec_t FPDEC_ONE_HUNDRED = {
    .dyn_alloc = false,
    .normalized = false,
    .sign = 1,
    .dec_prec = 0,
    .hi = 0,
    .lo = 100,
};


/*****************************************************************************
*  Macros
*****************************************************************************/

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
        if (fpdec->digit_array == NULL)
            MEMERROR;
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

    if (n_chars == 0)
        ERROR(FPDEC_INVALID_DECIMAL_LITERAL);

    if (n_chars <= COEFF_SIZE_THRESHOLD) {
        dec_repr = &st_dec_repr;
    }
    else {
        dec_repr = fpdec_mem_alloc(offsetof(dec_repr_t, coeff) + n_chars, 1);
        if (dec_repr == NULL)
            MEMERROR;
    }
    rc = parse_ascii_dec_literal(dec_repr, literal);
    if (rc != FPDEC_OK)
        goto EXIT;

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

    if (size == 0)
        ERROR(FPDEC_INVALID_DECIMAL_LITERAL);

    ch = buf = fpdec_mem_alloc(size + 1, sizeof(char));
    if (buf == NULL)
        MEMERROR;

    for (; iswspace(*literal); ++literal);
    for (; *literal != 0; ++literal, ++ch) {
        if (*literal > 255) {
            *ch = lookup_unicode_digit(*literal);
            if (*ch == -1) {
                fpdec_mem_free(buf);
                ERROR(FPDEC_INVALID_DECIMAL_LITERAL);
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

// Functions converting between FPDECs internal representations

static fpdec_n_digits_t
du64_to_digits(fpdec_digit_t *digit, int *n_trailing_zeros_skipped,
               uint64_t lo, uint64_t hi, int prec) {
    uint128_t t = U128_RHS(lo, hi);
    fpdec_n_digits_t n_digits = 0;

    assert(lo != 0 || hi != 0);
    assert(prec <= MAX_N_DEC_DIGITS_IN_SHINT);

    *n_trailing_zeros_skipped = 0;
    if (prec > 0) {
        *digit = u128_idiv_u32(&t, u64_10_pow_n(prec));
        if (*digit != 0) {
            *digit *= u64_10_pow_n(UINT64_10_POW_N_CUTOFF - prec);
            n_digits++;
            digit++;
        }
        else {
            (*n_trailing_zeros_skipped)++;
        }
    }
    *digit = u128_idiv_radix(&t);
    if (*digit != 0 || U128_NE_ZERO(t) && n_digits > 0) {
        n_digits++;
        digit++;
    }
    if (U128_NE_ZERO(t)) {
        if (n_digits == 0)
            (*n_trailing_zeros_skipped)++;
        *digit = U128_LO(t);
        n_digits++;
    }
    return n_digits;
}

static error_t
fpdec_set_dyn_coeff(fpdec_t *fpdec,
                    const uint64_t lo, const uint64_t hi) {
    fpdec_digit_t digits[3];
    int n_trailing_zeros;
    fpdec_n_digits_t n_digits;
    error_t rc;

    n_digits = du64_to_digits(digits, &n_trailing_zeros, lo, hi,
                              FPDEC_DEC_PREC(fpdec));
    rc = digits_from_digits(&fpdec->digit_array, digits, n_digits);
    if (rc == FPDEC_OK) {
        fpdec->dyn_alloc = true;
        fpdec->normalized = true;
        fpdec->exp = n_trailing_zeros -
                     CEIL(FPDEC_DEC_PREC(fpdec), DEC_DIGITS_PER_DIGIT);
    }
    return rc;
}

static inline error_t
fpdec_shint_to_dyn(fpdec_t *fpdec) {
    assert(!FPDEC_IS_DYN_ALLOC(fpdec));

    return fpdec_set_dyn_coeff(fpdec, fpdec->lo, fpdec->hi);
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
            uint128_t shint = UINT128_ZERO;
            uint128_t f = UINT128_ZERO;
            fpdec_n_digits_t n_digits = FPDEC_DYN_N_DIGITS(fpdec);
            fpdec_n_digits_t digit_idx = 0;
            fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
            uint64_t dec_shift = u64_10_pow_n(dec_prec);
            switch FPDEC_DYN_EXP(fpdec) {
                case -1:
                    u64_mul_u64(&shint, digits[digit_idx], dec_shift);
                    u128_idiv_radix(&shint);
                    // shint < RADIX
                    if (++digit_idx == n_digits)
                        break;
                case 0:
                    u64_mul_u64(&f, digits[digit_idx], dec_shift);
                    // f < RADIX * 10^9
                    u128_iadd_u128(&shint, &f);
                    // shint < RADIX + RADIX * 10^9 < 2^96
                    if (++digit_idx == n_digits)
                        break;
                case 1:
                    u64_mul_u64(&f, digits[digit_idx], dec_shift);
                    // f < RADIX * 10^9
                    u128_imul_u64(&f, RADIX);
                    if (UINT128_CHECK_MAX(&f)) {
                        SIGNAL_OVERFLOW(&shint);
                        break;
                    }
                    u128_iadd_u128(&shint, &f);
                    if (u128_cmp(shint, f) < 0) {
                        SIGNAL_OVERFLOW(&shint);
                        break;
                    }
                    if (++digit_idx == n_digits)
                        break;
                default:
                    assert(digit_idx == n_digits);
            }
            if (U128_FITS_SHINT(shint)) {
                fpdec_reset_to_zero(fpdec, dec_prec);
                FPDEC_SIGN(fpdec) = sign;
                fpdec->lo = U128_LO(shint);
                fpdec->hi = U128_HI(shint);
            }
        }
    }
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
    if (FPDEC_EQ_ZERO(fpdec))
        ERROR_RETVAL(ERANGE, -1);
    errno = 0;
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

    x_n_digits = du64_to_digits(x_digits, &n_trailing_zeros_skipped,
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

    y_n_digits = du64_to_digits(y_digits, &n_trailing_zeros_skipped,
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
    if (rc == ENOMEM)
        MEMERROR;
    fpdec->sign = -src->sign;
    return FPDEC_OK;
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
        fpdec->hi = U128_HI(ui);
        fpdec->lo = U128_LO(ui);
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
            quant = u64_10_pow_n(dec_shift);
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
    while (adj_to < 0 && U128_FITS_SHINT(shifted)) {
        // shift back
        dec_shift = MIN(-adj_to, UINT64_10_POW_N_CUTOFF);
        u128_imul_u64(&shifted, u64_10_pow_n(dec_shift));
        adj_to += dec_shift;
    }

    if (!U128_FITS_SHINT(shifted)) {
        rc = fpdec_shint_to_dyn(fpdec);
        if (rc != FPDEC_OK)
            return rc;
        return fpdec_dyn_adjust_to_prec(fpdec, dec_prec, rounding);
    }
    else {
        if (U128_NE_ZERO(shifted)) {
            fpdec->lo = U128_LO(shifted);
            fpdec->hi = U128_HI(shifted);
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

    if (ABS(dec_prec) > FPDEC_MAX_DEC_PREC)
        ERROR(FPDEC_PREC_LIMIT_EXCEEDED);

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
    if (rc == ENOMEM)
        MEMERROR;

    rc = fpdec_adjust(fpdec, dec_prec, rounding);
    if (rc != FPDEC_OK)
        fpdec_reset_to_zero(fpdec, 0);
    return rc;
}

error_t
fpdec_quantize(fpdec_t *fpdec, fpdec_t *quant,
               enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    fpdec_t t1 = FPDEC_ZERO;
    fpdec_t t2 = FPDEC_ZERO;

    rc = fpdec_div(&t1, fpdec, quant, 0, rounding);
    if (rc != FPDEC_OK)
        return rc;
    rc = fpdec_mul(&t2, &t1, quant);
    if (rc == FPDEC_OK)
        fpdec_reset_to_zero(fpdec, 0);
    *fpdec = t2;
    fpdec_reset_to_zero(&t1, 0);
    return rc;
}

error_t
fpdec_quantized(fpdec_t *fpdec, const fpdec_t *src, fpdec_t *quant,
                enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;

    ASSERT_FPDEC_IS_ZEROED(fpdec);

    rc = fpdec_copy(fpdec, src);
    if (rc == ENOMEM)
        MEMERROR;

    rc = fpdec_quantize(fpdec, quant, rounding);
    if (rc != FPDEC_OK)
        fpdec_reset_to_zero(fpdec, 0);
    return rc;
}

static inline uint8_t *
fill_in_zeros(uint8_t *ch, const int n) {
    uint8_t *stop = ch + n;
    for (; ch < stop; *ch++ = '0');
    return stop;
}


static inline uint8_t *
fillin_n_bytes_to_the_left(uint8_t *buf, const uint8_t *src,
                           const uint8_t n) {
    buf -= n;
    memcpy(buf, src, n);
    return buf;
}

struct grouping_iter {
    const uint8_t *next;
};

static inline struct grouping_iter
init_grouping_iter(const uint8_t *grouping) {
    assert(grouping[4] == 0);
    struct grouping_iter it = {grouping};
    return it;
}

static inline uint8_t
iter_grouping(struct grouping_iter *it) {
    const uint8_t *next = it->next;
    uint8_t l = *next;
    if (l > 0 && *(next + 1) > 0)
        it->next += 1;
    if (l == CHAR_MAX)
        return 0;
    return l;
}

static inline uint8_t *
copy_utf8c(uint8_t *buf, utf8c_t utf8c) {
    for (uint8_t *ch = utf8c.bytes; ch < utf8c.bytes + utf8c.n_bytes;
         *buf++ = *ch++);
    return buf;
}

static inline uint8_t *
buf_align(uint8_t *buf, size_t n_char, size_t min_width, char sign,
          char align, utf8c_t fill) {
    assert(n_char < min_width);
    size_t n_to_fill = min_width - n_char;
    size_t n_lpad, n_rpad;
    uint8_t *ch, *offset;

    switch (align) {
        case '=':
            ch = buf + (sign == '\0' ? 0 : 1);
            n_lpad = n_to_fill;
            n_rpad = 0;
            break;
        case '>':
            ch = buf;
            n_lpad = n_to_fill;
            n_rpad = 0;
            break;
        case '<':
            ch = buf + n_char;
            n_lpad = 0;
            n_rpad = n_to_fill;
            break;
        case '^':
            ch = n_to_fill > 1 ? buf : buf + n_char;
            n_lpad = n_to_fill / 2;
            n_rpad = n_to_fill - n_lpad;
            break;
    }
    if (n_lpad > 0) {
        offset = ch + n_lpad * fill.n_bytes;
        memmove(offset, ch, n_char);
        for (; ch < offset; ch = copy_utf8c(ch, fill));
        ch += n_char;
    }
    if (n_rpad > 0) {
        offset = ch + n_rpad * fill.n_bytes;
        for (; ch < offset; ch = copy_utf8c(ch, fill));
    }
    return ch;
}

static inline uint8_t *
fill_in_digit(uint8_t *buf, fpdec_digit_t digit, const int n) {
    uint8_t *t = buf + n;
    for (uint8_t *ch = t - 1; ch >= buf; --ch) {
        *ch = '0' + (digit % 10);
        digit /= 10;
    }
    return t;
}

static inline uint8_t *
fill_in_u128(uint8_t *buf, uint128_t ui) {
    uint8_t dec_digits[MAX_N_DEC_DIGITS_IN_SHINT];
    uint8_t *ch = dec_digits;
    while (U128_NE_ZERO(ui))
        *ch++ = '0' + u128_idiv_10(&ui);
    while (--ch >= dec_digits)
        *buf++ = *ch;
    return buf;
}

static inline uint8_t *
fill_in_u128_padded(uint8_t *buf, uint128_t ui, utf8c_t sep,
                    struct grouping_iter *it, size_t min_width) {
    size_t len = MAX(MAX_N_DEC_DIGITS_IN_SHINT, min_width + 1) *
                 (1 + sep.n_bytes) + 1;
    uint8_t r2l_buf[len];
    uint8_t *ch = r2l_buf + len - 1;
    size_t n_char = 0;
    uint8_t i, n;

    *ch = '\0';
    i = n = iter_grouping(it);
    while (U128_NE_ZERO(ui) || n_char < min_width) {
        *(--ch) = '0' + u128_idiv_10(&ui);
        ++n_char;
        if (n > 0) {
            --i;
            if (i == 0 && (U128_NE_ZERO(ui) || n_char < min_width)) {
                ch = fillin_n_bytes_to_the_left(ch, sep.bytes, sep.n_bytes);
                ++n_char;
                i = n = iter_grouping(it);
            }
        }
    }
    if (*ch < '0' || *ch > '9')       // last char was a seperator
        *(--ch) = '0';
    // copy bytes to target buffer
    while (*ch != '\0')
        *buf++ = *ch++;
    return buf;
}

static uint8_t *
fpdec_shint_formatted(const fpdec_t *fpdec, const format_spec_t *fmt_spec,
                      const bool no_trailing_zeros) {
    uint8_t *buf;
    uint8_t *ch;
    size_t max_n_bytes;
    size_t n_char;
    uint8_t len_decimal_point = fmt_spec->decimal_point.n_bytes;
    size_t len_thousands_sep = fmt_spec->thousands_sep.n_bytes;
    size_t len_fill = fmt_spec->fill.n_bytes;
    uint8_t dec_point_shift = 0;
    error_t rc;
    fpdec_t adj = FPDEC_ZERO;
    fpdec_dec_prec_t dec_prec = FPDEC_DEC_PREC(fpdec);

    // shift decimal point for %-format
    if (fmt_spec->type == '%') {
        dec_point_shift = 2;
        dec_prec = dec_prec > 2 ? dec_prec - 2 : 0;
    }

    if (fmt_spec->precision < dec_prec) {
        // need to adjust value
        int32_t adj_prec = fmt_spec->precision + dec_point_shift;
        rc = fpdec_adjusted(&adj, fpdec, adj_prec, FPDEC_ROUND_DEFAULT);
        if (rc != FPDEC_OK)
            return NULL;
        fpdec = &adj;
        dec_prec = fmt_spec->precision;
    }

    max_n_bytes = MAX(
        (fmt_spec->sign == '\0' ? 0 : 1) +
        // maximum number of integral decimal digits (incl. provision for
        // multi-byte thousands sep character)
        (MAX_N_DEC_DIGITS_IN_SHINT - dec_prec) * (1 + len_thousands_sep) +
        // radix point
        len_decimal_point +
        // fractional digits
        fmt_spec->precision +
        // percent sign
        (fmt_spec->type == '%' ? 1 : 0),
    // min width (incl. provision for multi-byte fill character) +
    // provision for additional zero infront of a thousands separator
        fmt_spec->min_width * (1 + len_fill) + 1);
    ch = buf = fpdec_mem_alloc(max_n_bytes + 1, 1);
    if (buf == NULL)
        MEMERROR_RETVAL(NULL);

    // separate integral and fractional part
    uint128_t int_part = U128_FROM_SHINT(fpdec);
    uint64_t frac_part = 0;
    if (dec_prec > 0)
        // split shifted int
        frac_part = u128_idiv_u64(&int_part, u64_10_pow_n(dec_prec));

    if (fmt_spec->sign != '\0')
        *ch++ = fmt_spec->sign;
    // integral part
    if (len_thousands_sep == 0) {
        if (U128_EQ_ZERO(int_part)) {
            // atleast one integral digit
            *ch++ = '0';
        }
        else
            ch = fill_in_u128(ch, int_part);
    }
    else {
        struct grouping_iter it = init_grouping_iter(fmt_spec->grouping);
        size_t min_width_int_part = 0;
        if (fmt_spec->fill.bytes[0] == '0' && fmt_spec->align == '=') {
            // zero padding
            size_t n_non_int_part = fmt_spec->precision + len_decimal_point +
                                    (fmt_spec->sign == 0 ? 0 : 1) +
                                    (fmt_spec->type == '%' ? 1 : 0);
            if (fmt_spec->min_width > n_non_int_part)
                min_width_int_part = fmt_spec->min_width - n_non_int_part;
        }
        ch = fill_in_u128_padded(ch, int_part, fmt_spec->thousands_sep, &it,
                                 min_width_int_part);
    }
    // fractional part
    size_t n = fmt_spec->precision;
    if (no_trailing_zeros) {
        if (frac_part == 0)
            n = 0;
        else
            while (frac_part > 0 && (frac_part % 10) == 0) {
                frac_part /= 10;
                n--;
            }
        dec_prec = n;
    }
    // radix point (only if there are any fractional digits)
    if (n > 0) {
        ch = copy_utf8c(ch, fmt_spec->decimal_point);
        ch = fill_in_digit(ch, (fpdec_digit_t)frac_part, dec_prec);
        if (n > dec_prec)
            ch = fill_in_zeros(ch, n - dec_prec);
    }

    if (fmt_spec->type == '%')
        *ch++ = '%';

    if (fmt_spec->min_width > 0) {
        n_char = utf8_strlen(buf);
        if (n_char == SIZE_MAX) {
            fpdec_mem_free(buf);
            ERROR_RETVAL(FPDEC_INVALID_FORMAT, NULL);
        }
        if (fmt_spec->min_width > n_char) {
            ch = buf_align(buf, n_char, fmt_spec->min_width, fmt_spec->sign,
                           fmt_spec->align, fmt_spec->fill);
        }
    }

    fpdec_reset_to_zero(&adj, 0);
    assert(ch <= buf + max_n_bytes);
    assert(*ch == 0);
    return buf;
}

static inline uint8_t *
fill_in_leading_digit(uint8_t *buf, fpdec_digit_t digit) {
    uint8_t dec_digits[DEC_DIGITS_PER_DIGIT];
    uint8_t *ch = dec_digits;
    while (digit > 0) {
        *ch++ = '0' + (digit % 10);
        digit /= 10;
    }
    while (--ch >= dec_digits)
        *buf++ = *ch;
    return buf;
}

static inline uint8_t *
fill_in_digits_padded(uint8_t *buf, const fpdec_digit_t *most_signif_digit,
                      fpdec_n_digits_t n_digits, size_t n_trailing_zeros,
                      utf8c_t sep, struct grouping_iter *it,
                      size_t min_width) {
    const fpdec_digit_t *digit = most_signif_digit - n_digits + 1;
    size_t len = MAX(n_digits * DEC_DIGITS_PER_DIGIT + n_trailing_zeros,
                     min_width + 1) * (1 + sep.n_bytes) + 1;
    uint8_t r2l_buf[len];
    uint8_t *ch = r2l_buf + len - 1;
    size_t n_char = 0;
    uint8_t i, n;
    fpdec_digit_t t;

    *ch = '\0';
    i = n = iter_grouping(it);

    for (int j = 0; j < n_trailing_zeros; ++j) {
        *(--ch) = '0';
        if (n > 0) {
            --i;
            if (i == 0) {
                ch = fillin_n_bytes_to_the_left(ch, sep.bytes, sep.n_bytes);
                ++n_char;
                i = n = iter_grouping(it);
            }
        }
    }
    n_char += n_trailing_zeros;

    for (; digit < most_signif_digit; ++digit) {
        t = *digit;
        for (int j = 0; j < DEC_DIGITS_PER_DIGIT; ++j) {
            *(--ch) = '0' + (t % 10);
            t /= 10;
            if (n > 0) {
                --i;
                if (i == 0) {
                    ch = fillin_n_bytes_to_the_left(ch, sep.bytes,
                                                    sep.n_bytes);
                    ++n_char;
                    i = n = iter_grouping(it);
                }
            }
        }
        n_char += DEC_DIGITS_PER_DIGIT;
    }

    // most significant digit and zero padding
    if (n_digits > 0)
        t = *digit;
    else
        t = 0;
    while (t > 0 || n_char < min_width) {
        *(--ch) = '0' + (t % 10);
        t /= 10;
        ++n_char;
        if (n > 0) {
            --i;
            if (i == 0 && (t > 0 || n_char < min_width)) {
                ch = fillin_n_bytes_to_the_left(ch, sep.bytes, sep.n_bytes);
                ++n_char;
                i = n = iter_grouping(it);
            }
        }
    }
    if (*ch < '0' || *ch > '9')       // last char was a seperator
        *(--ch) = '0';
    // copy bytes to target buffer
    while (*ch != '\0')
        *buf++ = *ch++;
    return buf;
}

static uint8_t *
fpdec_dyn_formatted(const fpdec_t *fpdec, const format_spec_t *fmt_spec,
                    const bool no_trailing_zeros) {
    uint8_t *buf;
    uint8_t *ch;
    size_t max_n_bytes;
    size_t n_char;
    uint8_t len_decimal_point = fmt_spec->decimal_point.n_bytes;
    size_t len_thousands_sep = fmt_spec->thousands_sep.n_bytes;
    size_t len_fill = fmt_spec->fill.n_bytes;
    error_t rc;
    fpdec_t adj = FPDEC_ZERO;
    fpdec_dec_prec_t dec_prec;
    fpdec_exp_t exp;
    fpdec_n_digits_t n_int_digits, n_frac_digits;
    size_t n_dec_trailing_int_zeros, n_dec_trailing_frac_zeros;
    size_t n_dec_frac_fill_zeros;
    size_t n_dec_frac_digits, d_adjust;

    if (fmt_spec->type == '%') {
        rc = fpdec_mul(&adj, fpdec, &FPDEC_ONE_HUNDRED);
        if (rc != FPDEC_OK)
            return NULL;
        FPDEC_DEC_PREC(&adj) = FPDEC_DEC_PREC(&adj) > 2 ?
                               FPDEC_DEC_PREC(&adj) - 2 : 0;
        fpdec = &adj;
    }

    if (fmt_spec->precision < FPDEC_DEC_PREC(fpdec)) {
        // need to adjust value
        if (fpdec == &adj)
            rc = fpdec_adjust(&adj, fmt_spec->precision, FPDEC_ROUND_DEFAULT);
        else {
            rc = fpdec_adjusted(&adj, fpdec, fmt_spec->precision,
                                FPDEC_ROUND_DEFAULT);
            if (!FPDEC_IS_DYN_ALLOC(&adj)) {
                buf = fpdec_shint_formatted(&adj, fmt_spec,
                                            no_trailing_zeros);
                fpdec_reset_to_zero(&adj, 0);
                return buf;
            }
            fpdec = &adj;
        }
        if (rc != FPDEC_OK)
            return NULL;
    }

    dec_prec = FPDEC_DEC_PREC(fpdec);
    exp = FPDEC_DYN_EXP(fpdec);
    fpdec_digit_t *digit =
        FPDEC_DYN_DIGITS(fpdec) + FPDEC_DYN_N_DIGITS(fpdec) - 1;

    if (exp >= 0) {
        n_int_digits = FPDEC_DYN_N_DIGITS(fpdec);
        n_dec_trailing_int_zeros = exp * DEC_DIGITS_PER_DIGIT;
        n_frac_digits = 0;
        n_dec_frac_digits = 0;
        n_dec_frac_fill_zeros = 0;
    }
    else {
        if (-exp > FPDEC_DYN_N_DIGITS(fpdec)) {
            n_int_digits = 0;
            n_frac_digits = FPDEC_DYN_N_DIGITS(fpdec);
            n_dec_frac_fill_zeros =
                (-exp - n_frac_digits) * DEC_DIGITS_PER_DIGIT;
        }
        else {
            n_int_digits = FPDEC_DYN_N_DIGITS(fpdec) + exp;
            n_frac_digits = -exp;
            n_dec_frac_fill_zeros = 0;
        }
        n_dec_trailing_int_zeros = 0;
        n_dec_frac_digits = -exp * DEC_DIGITS_PER_DIGIT;
    }
    if (n_dec_frac_digits > dec_prec) {
        d_adjust = n_dec_frac_digits - dec_prec;
        n_dec_trailing_frac_zeros = 0;
    }
    else {
        d_adjust = 0;
        if (no_trailing_zeros)
            n_dec_trailing_frac_zeros = 0;
        else
            n_dec_trailing_frac_zeros = dec_prec - n_dec_frac_digits;
    }
    max_n_bytes = MAX(
        (fmt_spec->sign == '\0' ? 0 : 1) +
        // maximum number of integral decimal digits (incl. provision for
        // multi-byte thousands sep character)
        MAX((// maxinum integral digits in coeff
                n_int_digits * DEC_DIGITS_PER_DIGIT +
                // zeros to be inserted after least significant digit and
                // before radix point
                n_dec_trailing_int_zeros) * (1 + len_thousands_sep),
            1) +
        // radix point
        len_decimal_point +
        // fractional digits
        fmt_spec->precision +
        // percent sign
        (fmt_spec->type == '%' ? 1 : 0),
    // min width (incl. provision for multi-byte fill character) +
    // provision for additional zero infront of a thousands separator
        fmt_spec->min_width * (1 + len_fill) + 1);
    ch = buf = fpdec_mem_alloc(max_n_bytes + 1, 1);
    if (buf == NULL)
        MEMERROR_RETVAL(NULL);

    if (fmt_spec->sign != '\0')
        *ch++ = fmt_spec->sign;
    // integral part
    if (len_thousands_sep == 0) {
        if (n_int_digits == 0) {
            // atleast one integral digit
            *ch++ = '0';
        }
        else {
            ch = fill_in_leading_digit(ch, *digit--);
            for (; digit >= FPDEC_DYN_DIGITS(fpdec) + n_frac_digits; --digit)
                ch = fill_in_digit(ch, *digit, DEC_DIGITS_PER_DIGIT);
            ch = fill_in_zeros(ch, n_dec_trailing_int_zeros);
        }
    }
    else {
        struct grouping_iter it = init_grouping_iter(fmt_spec->grouping);
        size_t min_width_int_part = 0;
        if (fmt_spec->fill.bytes[0] == '0' && fmt_spec->align == '=') {
            // zero padding
            size_t n_non_int_part = fmt_spec->precision + len_decimal_point +
                                    (fmt_spec->sign == 0 ? 0 : 1) +
                                    (fmt_spec->type == '%' ? 1 : 0);
            if (fmt_spec->min_width > n_non_int_part)
                min_width_int_part = fmt_spec->min_width - n_non_int_part;
        }
        ch = fill_in_digits_padded(ch, digit, n_int_digits,
                                   n_dec_trailing_int_zeros,
                                   fmt_spec->thousands_sep, &it,
                                   min_width_int_part);
    }
    // fractional part
    size_t n = (no_trailing_zeros && n_frac_digits == 0) ? 0 :
               fmt_spec->precision;
    // radix point (only if there are any fractional digits)
    if (n > 0) {
        ch = copy_utf8c(ch, fmt_spec->decimal_point);
        // zeros between radix point and fractional digits
        ch = fill_in_zeros(ch, n_dec_frac_fill_zeros);
        // full fractional digits
        for (digit = FPDEC_DYN_DIGITS(fpdec) + n_frac_digits - 1;
             digit > FPDEC_DYN_DIGITS(fpdec); --digit) {
            ch = fill_in_digit(ch, *digit, DEC_DIGITS_PER_DIGIT);
        }
        // least significant fractional digit
        if (n_frac_digits > 0) {
            digit = FPDEC_DYN_DIGITS(fpdec);
            int t = DEC_DIGITS_PER_DIGIT - d_adjust;
            fpdec_digit_t adj_digit = *digit / u64_10_pow_n(d_adjust);
            if (no_trailing_zeros)
                while ((adj_digit % 10) == 0 && t-- > 0)
                    adj_digit /= 10;
            ch = fill_in_digit(ch, adj_digit, t);
        }
        // trailing fractional zeros
        if (n > dec_prec)
            n_dec_trailing_frac_zeros += n - dec_prec;
        ch = fill_in_zeros(ch, n_dec_trailing_frac_zeros);
    }

    if (fmt_spec->type == '%')
        *ch++ = '%';

    if (fmt_spec->min_width > 0) {
        n_char = utf8_strlen(buf);
        if (n_char == SIZE_MAX) {
            fpdec_mem_free(buf);
            ERROR_RETVAL(FPDEC_INVALID_FORMAT, NULL);
        }
        if (fmt_spec->min_width > n_char) {
            ch = buf_align(buf, n_char, fmt_spec->min_width, fmt_spec->sign,
                           fmt_spec->align, fmt_spec->fill);
        }
    }

    fpdec_reset_to_zero(&adj, 0);
    assert(ch <= buf + max_n_bytes);
    assert(*ch == 0);
    return buf;
}

typedef uint8_t *(*v_formatted)(const fpdec_t *, const format_spec_t *,
                                const bool);

const v_formatted vtab_formatted[2] = {
    fpdec_shint_formatted,
    fpdec_dyn_formatted,
};

uint8_t *
fpdec_formatted(const fpdec_t *fpdec, const uint8_t *format) {
    format_spec_t fmt_spec;
    uint8_t dec_point_shift = 0;
    int rc;

    rc = parse_format_spec(&fmt_spec, format);
    if (rc == -1)
        ERROR_RETVAL(FPDEC_INVALID_FORMAT, NULL);
    if (rc == -2)
        ERROR_RETVAL(FPDEC_INCOMPAT_LOCALE, NULL);

    // sign to be shown?
    if (FPDEC_LT_ZERO(fpdec))
        fmt_spec.sign = '-';        // always show sign for negative numbers
    else if (fmt_spec.sign == '-')
        fmt_spec.sign = '\0';       // suppress positive sign

    // precision and decimal point
    if (fmt_spec.type == '%')
        dec_point_shift = 2;
    if (fmt_spec.precision == SIZE_MAX)
        fmt_spec.precision = FPDEC_DEC_PREC(fpdec) > dec_point_shift ?
                             FPDEC_DEC_PREC(fpdec) - dec_point_shift : 0;
    if (fmt_spec.precision == 0)                    // if number is integral
        fmt_spec.decimal_point.n_bytes = 0;         // suppress decimal point

    return DISPATCH_FUNC_VA(vtab_formatted, fpdec, &fmt_spec, false);
}

char *
fpdec_as_ascii_literal(const fpdec_t *fpdec,
                       const bool no_trailing_zeros) {
    format_spec_t fmt_spec = {
        .fill = {0, ""},
        .align = '<',
        .sign = FPDEC_LT_ZERO(fpdec) ? '-' : '\0',
        .min_width = 0,
        .thousands_sep = {0, ""},
        .grouping = {3, 0, 0, 0, 0},
        .decimal_point = {FPDEC_DEC_PREC(fpdec) == 0 ? 0 : 1, '.'},
        .precision = FPDEC_DEC_PREC(fpdec),
        .type = 'f'
    };
    return (char *)DISPATCH_FUNC_VA(vtab_formatted, fpdec, &fmt_spec,
                                    no_trailing_zeros);
}

int
fpdec_as_sign_coeff128_exp(fpdec_sign_t *sign, uint128_t *coeff, int64_t *exp,
                           const fpdec_t *fpdec) {
    *sign = FPDEC_SIGN(fpdec);
    if (*sign == 0) {
        *coeff = UINT128_ZERO;
        *exp = 0ULL;
        return 0;
    }
    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        fpdec_n_digits_t n = FPDEC_DYN_N_DIGITS(fpdec);
        fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
        uint128_t t1;
        int ntz, nsd;

        *exp = FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT;
        switch (n) {
            case 1:
                U128_FROM_LO_HI(coeff, digits[0], 0UL);
                break;
            case 2:
                U128_FROM_LO_HI(coeff, digits[1], 0UL);
                // coeff < RADIX
                u128_imul_10_pow_n(coeff, DEC_DIGITS_PER_DIGIT);
                // coeff < RADIX * RADIX
                u128_iadd_u64(coeff, digits[0]);
                // coeff < RADIX * RADIX + RADIX < 2^128
                break;
            case 3:
                // try to fit normalized coeff into uint128
                U128_FROM_LO_HI(&t1, digits[0], 0ULL);
                ntz = u128_eliminate_trailing_zeros(&t1,
                                                    DEC_DIGITS_PER_DIGIT);
                nsd = DEC_DIGITS_PER_DIGIT - ntz;
                U128_FROM_LO_HI(coeff, U128_LO(t1), 0UL);
                // coeff < RADIX
                U128_FROM_LO_HI(&t1, digits[1], 0UL);
                u128_imul_10_pow_n(&t1, nsd);
                // t1 < RADIX * RADIX
                u128_iadd_u128(coeff, &t1);
                // coeff < RADIX * RADIX + RADIX < 2^128
                U128_FROM_LO_HI(&t1, digits[2], 0UL);
                u128_imul_u64(&t1, RADIX);
                u128_imul_10_pow_n(&t1, nsd);
                if (UINT128_CHECK_MAX(&t1))
                    // overflow
                    return -1;
                u128_iadd_u128(coeff, &t1);
                if (u128_cmp(*coeff, t1) < 0)
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
        U128_FROM_LO_HI(coeff, fpdec->lo, fpdec->hi);
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
        // y_shint < 2^96 and 0 <= shift <= 9
        u128_imul_10_pow_n(y_shint, shift);
        // y_shint < 2^96 * 10^9 < 2^128
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
    z->lo = U128_LO(x_shint);
    z->hi = U128_HI(x_shint);
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
            if (z_digits == NULL)
                MEMERROR;
            s_digits = y->digit_array;
        }
        else {
            z_digits = digits_copy(y->digit_array, n_shift, n_add_zeros);
            if (z_digits == NULL)
                MEMERROR;
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
        if (z_digits == NULL)
            MEMERROR;
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
        if (z_digits == NULL)
            MEMERROR;
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
    z->lo = U128_LO(x_shint);
    z->hi = U128_HI(x_shint);
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
        if (z_digits == NULL)
            MEMERROR;
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(x);
    }
    else if (FPDEC_DYN_EXP(x) > FPDEC_DYN_EXP(y)) {
        n_shift = FPDEC_DYN_EXP(x) - FPDEC_DYN_EXP(y);
        z_digits = digits_copy(x->digit_array, n_shift, 0);
        if (z_digits == NULL)
            MEMERROR;
        s_digits = y->digit_array;
        z_exp = FPDEC_DYN_EXP(y);
    }
    else {
        z_digits = digits_copy(x->digit_array, 0, 0);
        if (z_digits == NULL)
            MEMERROR;
        n_shift = FPDEC_DYN_EXP(y) - FPDEC_DYN_EXP(x);
        s_digits = digits_copy(y->digit_array, n_shift, 0);
        if (s_digits == NULL) {
            fpdec_mem_free((void *)z_digits);
            MEMERROR;
        }
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

    if (z_digits == NULL)
        MEMERROR;

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
        z->lo = U128_LO(z_shint);
        z->hi = U128_HI(z_shint);
        return FPDEC_OK;
    }
    return FPDEC_N_DIGITS_LIMIT_EXCEEDED;
}

static error_t
fpdec_mul_abs_dyn_by_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y) {
    fpdec_digit_array_t *z_digits;
    int64_t exp;

    z_digits = digits_mul(x->digit_array, y->digit_array);
    if (z_digits == NULL)
        MEMERROR;
    exp = (int64_t)FPDEC_DYN_EXP(x) + (int64_t)FPDEC_DYN_EXP(y);
    if (exp > FPDEC_MAX_EXP || exp < INT32_MIN) {
        fpdec_mem_free((void *)z_digits);
        ERROR(FPDEC_EXP_LIMIT_EXCEEDED);
    }
    z->exp = (fpdec_exp_t)exp;
    z->digit_array = z_digits;
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
    if (FPDEC_DEC_PREC(z) < FPDEC_DEC_PREC(x))
        ERROR(FPDEC_PREC_LIMIT_EXCEEDED);
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
        if (q_digits == NULL)
            MEMERROR;
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
        if (q_digits == NULL) {
            fpdec_mem_free(r_digits);
            MEMERROR;
        }
        if (r_digits == NULL) {
            fpdec_mem_free(q_digits);
            MEMERROR;
        }
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
            fpdec_reset_to_zero(&t, 0);
            if (rc != FPDEC_OK) {
                fpdec_mem_free(q_digits);
                return rc;
            }
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
    u128_idiv_u128(&r_shint, &q_shint, &y_shint);
    // adjust negativ quotient?
    if (neg_quot && U128_NE_ZERO(r_shint)) {
        u128_incr(&q_shint);
        u128_isub_u128(&y_shint, &r_shint);
        r_shint = y_shint;
    }
    if (U128_FITS_SHINT(q_shint)) {
        q->lo = U128_LO(q_shint);
        q->hi = U128_HI(q_shint);
        r->lo = U128_LO(r_shint);
        r->hi = U128_HI(r_shint);
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

    if (FPDEC_EQ_ZERO(y))
        ERROR(FPDEC_DIVIDE_BY_ZERO);

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

    if (exp < FPDEC_MIN_EXP)
        ERROR(FPDEC_PREC_LIMIT_EXCEEDED);

    q_digits = digits_div_max_prec(x->digit_array, y->digit_array,
                                   &exp);
    if (q_digits == NULL)
        MEMERROR;
    if (exp > FPDEC_MAX_EXP) {
        fpdec_mem_free((void *)q_digits);
        ERROR(FPDEC_EXP_LIMIT_EXCEEDED);
    }
    if (exp <= FPDEC_MIN_EXP) {
        fpdec_mem_free((void *)q_digits);
        ERROR(FPDEC_PREC_LIMIT_EXCEEDED);
    }
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
            if (q_digits == NULL)
                MEMERROR;
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
fpdec_div_shints_as_dyn(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                        const int prec_limit,
                        const enum FPDEC_ROUNDING_MODE rounding) {
    error_t rc;
    fpdec_t x_dyn;
    fpdec_t y_dyn;

    rc = fpdec_copy_shint_as_dyn(&x_dyn, x);
    if (rc == FPDEC_OK) {
        rc = fpdec_copy_shint_as_dyn(&y_dyn, y);
        if (rc == FPDEC_OK) {
            rc = fpdec_div_abs_dyn_by_dyn(z, &x_dyn, &y_dyn, prec_limit,
                                          rounding);
            fpdec_reset_to_zero(&y_dyn, 0);
        }
        fpdec_reset_to_zero(&x_dyn, 0);
    }
    return rc;
}

error_t
fpdec_div_abs_shint_by_shint(fpdec_t *z, const fpdec_t *x, const fpdec_t *y,
                             const int prec_limit,
                             const enum FPDEC_ROUNDING_MODE rounding) {
    uint128_t divident = U128_FROM_SHINT(x);
    uint128_t divisor = U128_FROM_SHINT(y);
    uint128_t rem = UINT128_ZERO;
    int shift;
    unsigned n_trailing_zeros;

    if (prec_limit == -1)
        shift = MAX_DEC_PREC_FOR_SHINT - FPDEC_DEC_PREC(x) +
                FPDEC_DEC_PREC(y);
    else
        shift = MIN(prec_limit, MAX_DEC_PREC_FOR_SHINT) - FPDEC_DEC_PREC(x) +
                FPDEC_DEC_PREC(y);
    if (shift > 0) {
        u128_imul_10_pow_n(&divident, shift);
        if (UINT128_CHECK_MAX(&divident))
            // divident possibly overflowed
            return fpdec_div_shints_as_dyn(z, x, y, prec_limit, rounding);
    }
    else if (shift < 0)
        // divisor < 2^96 and shift >= -9 => divisor * 10^-shift < 2^128
        u128_imul_10_pow_n(&divisor, -shift);
    u128_idiv_u128(&rem, &divident, &divisor);
    if (U128_NE_ZERO(rem)) {
        if (prec_limit == -1 || prec_limit > MAX_DEC_PREC_FOR_SHINT) {
            // result is not exact enough
            return fpdec_div_shints_as_dyn(z, x, y, prec_limit, rounding);
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
            shift = MIN(shift, MAX_DEC_PREC_FOR_SHINT);
            n_trailing_zeros = u128_eliminate_trailing_zeros(&divident,
                                                             shift);
            FPDEC_DEC_PREC(z) = MAX_DEC_PREC_FOR_SHINT - n_trailing_zeros;
        }
        else if (prec_limit > MAX_DEC_PREC_FOR_SHINT) {
            FPDEC_DEC_PREC(z) = MAX_DEC_PREC_FOR_SHINT;
            z->lo = U128_LO(divident);
            z->hi = U128_HI(divident);
            fpdec_shint_to_dyn(z);
            FPDEC_DEC_PREC(z) = prec_limit;
            return FPDEC_OK;
        }
        else
            FPDEC_DEC_PREC(z) = prec_limit;
    }
    if (U128_FITS_SHINT(divident)) {
        z->lo = U128_LO(divident);
        z->hi = U128_HI(divident);
        return FPDEC_OK;
    }
    else
        return fpdec_set_dyn_coeff(z, U128_LO(divident), U128_HI(divident));
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

    if (FPDEC_EQ_ZERO(y))
        ERROR(FPDEC_DIVIDE_BY_ZERO);

    if (FPDEC_EQ_ZERO(x))
        return FPDEC_OK;

    FPDEC_SIGN(z) = FPDEC_SIGN(x) * FPDEC_SIGN(y);
    rc = DISPATCH_BIN_OP_VA(vtab_div_abs, z, x, y, prec_limit, rounding);
    if (rc != FPDEC_OK)
        return rc;

    if (FPDEC_IS_DYN_ALLOC(z))
        fpdec_dyn_normalize(z);
    else {
        if (z->lo == 0 && z->hi == 0)
            FPDEC_SIGN(z) = FPDEC_SIGN_ZERO;
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
