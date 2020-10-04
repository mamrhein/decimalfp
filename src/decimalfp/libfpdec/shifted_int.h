/*
------------------------------------------------------------------------------
Name:        shifted_int.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/


#ifndef FPDEC_SHIFTED_INT_H
#define FPDEC_SHIFTED_INT_H

#include <math.h>

#include "helper_macros.h"
#include "basemath.h"
#include "rounding_helper.h"

/*****************************************************************************
*  Macros
*****************************************************************************/

#define MAX_N_DEC_DIGITS_IN_SHINT 29
#define MAX_DEC_PREC_FOR_SHINT 9

#define U128_NE_ZERO(x) (x.lo != 0 || x.hi != 0)

#define U128_FITS_SHINT(x) (U64_HI(x.hi) == 0)

#define U64_MAGNITUDE(x) ((int) log10(x))
#define U128_MAGNITUDE(lo, hi) ((int) log10(((double) hi) * 0x100000000UL \
                                * 0x100000000UL + (double) lo))

/*****************************************************************************
*  Functions
*****************************************************************************/

// Comparison

int
shint_cmp_abs(uint128_t x, fpdec_dec_prec_t x_prec,
              uint128_t y, fpdec_dec_prec_t y_prec);

// Converter

error_t
shint_from_dec_coeff(uint64_t *lo, uint32_t *hi, const dec_digit_t *coeff,
                     size_t n_dec_digits, size_t n_add_zeros);

fpdec_n_digits_t
u128_to_digits(fpdec_digit_t *digit, int *n_trailing_zeros_skipped,
               uint64_t b, uint64_t lo, uint64_t hi, int prec);

// Decimal shift

void
u128_idecshift(uint128_t *ui, fpdec_sign_t sign, int32_t n_dec_digits,
               enum FPDEC_ROUNDING_MODE rounding);

unsigned
u128_eliminate_trailing_zeros(uint128_t *ui, unsigned n_max);

#endif //FPDEC_SHIFTED_INT_H
