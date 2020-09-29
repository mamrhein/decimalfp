/*
------------------------------------------------------------------------------
Name:        shifted_int.c

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

#include "shifted_int.h"
#include "fpdec.h"

/*****************************************************************************
*  Macros
*****************************************************************************/


/*****************************************************************************
*  Functions
*****************************************************************************/

// Comparison

int
shint_cmp_abs(uint128_t x, fpdec_dec_prec_t x_prec,
              uint128_t y, fpdec_dec_prec_t y_prec) {
    if (x_prec < y_prec)
        u128_imul_10_pow_n(&x, y_prec - x_prec);
    else if (y_prec < x_prec)
        u128_imul_10_pow_n(&y, x_prec - y_prec);
    return u128_cmp(&x, &y);
}

// Converter

static inline void
u128_imul10(uint128_t *x) {
    x->hi = x->hi * 10 +
            U64_HI(U64_HI(x->lo) * 10UL + U64_HI(U64_LO(x->lo) * 10UL));
    x->lo *= 10UL;
}

error_t
shint_from_dec_coeff(uint64_t *lo, uint32_t *hi, const dec_digit_t *coeff,
                     const size_t n_dec_digits, const size_t n_add_zeros) {
    uint128_t accu = {0, 0};
    const dec_digit_t *stop = coeff + n_dec_digits;
    const dec_digit_t *cut = MIN(coeff + UINT64_10_POW_N_CUTOFF, stop);

    for (; coeff < cut; ++coeff) {
        accu.lo *= 10;
        accu.lo += *coeff;           // *coeff is < 10, so no overflow here
    }
    for (; coeff < stop; ++coeff) {
        u128_imul10(&accu);
        u128_iadd_u64(&accu, *coeff);
    }
    for (int i = 0; i < n_add_zeros; ++i) {
        u128_imul10(&accu);
    }
    if (U64_HI(accu.hi)) return FPDEC_N_DIGITS_LIMIT_EXCEEDED;
    *lo = accu.lo;
    *hi = (uint32_t) accu.hi;
    return FPDEC_OK;
}

fpdec_n_digits_t
shint_to_digits(fpdec_digit_t *digit, int *n_trailing_zeros_skipped,
                uint64_t b, uint64_t lo, uint32_t hi, int prec) {
    uint128_t t = {lo, hi};
    fpdec_n_digits_t n_digits = 0;

    assert(lo != 0 || hi != 0);
    assert(prec <= UINT64_10_POW_N_CUTOFF);

    *n_trailing_zeros_skipped = 0;
    if (prec > 0) {
        *digit = u128_idiv_u64(&t, _10_POW_N(prec));
        if (*digit != 0) {
            *digit *= _10_POW_N(UINT64_10_POW_N_CUTOFF - prec);
            n_digits++;
            digit++;
        }
        else {
            (*n_trailing_zeros_skipped)++;
        }
    }
    if (t.hi == 0) {
        *digit = t.lo % b;
        t.lo /= b;
    }
    else {
        *digit = u128_idiv_u64(&t, b);
    }
    if (*digit != 0 || t.lo != 0 && n_digits > 0) {
        n_digits++;
        digit++;
    }
    if (t.lo != 0) {
        if (n_digits == 0)
            (*n_trailing_zeros_skipped)++;
        *digit = t.lo;
        n_digits++;
    }
    return n_digits;
}

// Decimal shift

static void
u128_idivr_10_pow_n(uint128_t *x, const fpdec_sign_t sign, const uint8_t n,
                    const enum FPDEC_ROUNDING_MODE rounding) {
    uint64_t rem, divisor;

    assert(n <= UINT64_10_POW_N_CUTOFF);

    divisor = _10_POW_N(n);
    if (x->hi != 0)
        rem = u128_idiv_u64(x, divisor);
    else {
        rem = x->lo % divisor;
        x->lo /= divisor;
    }
    if (rem > 0 && round_qr(sign, x->lo, rem, false, divisor, rounding) > 0)
        u128_incr(x);
}

void
u128_idecshift(uint128_t *ui, fpdec_sign_t sign, int32_t n_dec_digits,
               enum FPDEC_ROUNDING_MODE rounding) {
    assert(n_dec_digits >= -MAX_N_DEC_DIGITS_IN_SHINT);
    assert(n_dec_digits <= UINT64_10_POW_N_CUTOFF);

    if (n_dec_digits > 0) {
        u128_imul_10_pow_n(ui, n_dec_digits);
        return;
    }

    if (n_dec_digits < 0)  {
        n_dec_digits = -n_dec_digits;
        int32_t dec_shift = MIN(n_dec_digits, UINT64_10_POW_N_CUTOFF);
        if (dec_shift < n_dec_digits) {
            u128_idivr_10_pow_n(ui, sign, dec_shift, FPDEC_ROUND_DOWN);
            dec_shift = n_dec_digits - dec_shift;
        }
        u128_idivr_10_pow_n(ui, sign, dec_shift, rounding);
    }
}

unsigned
u128_eliminate_trailing_zeros(uint128_t *ui, unsigned n_max) {
    uint128_t t = {ui->lo, ui->hi};
    unsigned n_trailing_zeros = 0;

    while (ui->hi != 0 && n_trailing_zeros < n_max && u128_idiv_10(&t) == 0) {
        ui->lo = t.lo;
        ui->hi = t.hi;
        n_trailing_zeros++;
    }
    if (ui->hi == 0) {
        while (n_trailing_zeros < n_max && ui->lo % 10 == 0) {
            ui->lo /= 10;
            n_trailing_zeros++;
        }
    }
    return n_trailing_zeros;
}
