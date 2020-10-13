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
    return u128_cmp(x, y);
}

// Converter

error_t
shint_from_dec_coeff(uint64_t *lo, uint32_t *hi, const dec_digit_t *coeff,
                     const size_t n_dec_digits, const size_t n_add_zeros) {
    const dec_digit_t *stop = coeff + n_dec_digits;
    const dec_digit_t *cut = MIN(coeff + UINT64_10_POW_N_CUTOFF, stop);
    uint64_t t;

    *hi = 0;
    *lo = *coeff;
    coeff++;

    for (; coeff < cut; ++coeff) {
        *lo *= 10UL;
        *lo += *coeff;              // *coeff is < 10, so no overflow here
    }
    for (; coeff < stop; ++coeff) {
        t = *hi * 10UL +
            U64_HI(U64_HI(*lo) * 10UL + U64_HI(U64_LO(*lo) * 10UL));
        *lo *= 10UL;
        *lo += *coeff;
        if (*lo < *coeff)
            t++;
        if (U64_HI(t))
            goto OVERFLOW;
        *hi = t;
    }
    if (n_add_zeros > 0) {
        uint8_t n_left_to_shift = n_add_zeros;
        uint8_t n_shift;
        uint128_t sh;
        U128_FROM_LO_HI(&sh, *lo, *hi);
        while (n_left_to_shift > 0) {
            n_shift = MIN(n_left_to_shift, UINT64_10_POW_N_CUTOFF);
            u128_imul_10_pow_n(&sh, n_shift);
            if (U64_HI(U128_HI(sh)))
                goto OVERFLOW;
            n_left_to_shift -= n_shift;
        }
        *hi = (uint32_t)U128_HI(sh);
        *lo = U128_LO(sh);
    }
    return FPDEC_OK;

OVERFLOW:
    *hi = 0;
    *lo = 0;
    return FPDEC_N_DIGITS_LIMIT_EXCEEDED;
}

// Decimal shift

static void
u128_idivr_10_pow_n(uint128_t *x, const fpdec_sign_t sign, const uint8_t n,
                    const enum FPDEC_ROUNDING_MODE rounding) {
    uint64_t rem, divisor;

    assert(n <= UINT64_10_POW_N_CUTOFF);

    divisor = u64_10_pow_n(n);
    rem = u128_idiv_u64(x, divisor);
    if (rem > 0 && round_qr(sign, U128P_LO(x), rem, false, divisor, rounding)
                   > 0)
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

    if (n_dec_digits < 0) {
        n_dec_digits = -n_dec_digits;
        int32_t dec_shift = MIN(n_dec_digits, UINT64_10_POW_N_CUTOFF);
        if (dec_shift < n_dec_digits) {
            u128_idivr_10_pow_n(ui, sign, dec_shift, FPDEC_ROUND_DOWN);
            dec_shift = n_dec_digits - dec_shift;
        }
        u128_idivr_10_pow_n(ui, sign, dec_shift, rounding);
    }
}
