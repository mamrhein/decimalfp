/*
------------------------------------------------------------------------------
Name:        fpdec.h
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

#ifndef FPDEC_H
#define FPDEC_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "common.h"
#include "mem.h"
#include "rounding.h"


/*****************************************************************************
*  Constants
*****************************************************************************/

extern const fpdec_t FPDEC_ZERO;
extern const fpdec_t FPDEC_ONE;
extern const fpdec_t FPDEC_MINUS_ONE;

/*****************************************************************************
*  Functions
*****************************************************************************/

// For testing only!

void
fpdec_dump(const fpdec_t *fpdec);

// Constructor

fpdec_t *
fpdec_new();

// Initializer

error_t
fpdec_copy(fpdec_t *fpdec, const fpdec_t *src);

error_t
fpdec_from_ascii_literal(fpdec_t *fpdec, const char *literal);

error_t
fpdec_from_unicode_literal(fpdec_t *fpdec, const wchar_t *literal);

error_t
fpdec_from_long_long(fpdec_t *fpdec, long long val);

error_t
fpdec_from_sign_digits_exp(fpdec_t *fpdec, fpdec_sign_t sign,
                           size_t n_digits,
                           const fpdec_digit_t *digits,
                           fpdec_exp_t exp);

// Properties

int
fpdec_magnitude(const fpdec_t *fpdec);

// Comparison

int
fpdec_compare(const fpdec_t *x, const fpdec_t *y, bool ignore_sign);

// Converter

error_t
fpdec_neg(fpdec_t *fpdec, const fpdec_t *src);

error_t
fpdec_normalize_prec(fpdec_t *fpdec);

error_t
fpdec_adjust(fpdec_t *fpdec, int32_t dec_prec,
             enum FPDEC_ROUNDING_MODE rounding);

error_t
fpdec_adjusted(fpdec_t *fpdec, const fpdec_t *src, int32_t dec_prec,
               enum FPDEC_ROUNDING_MODE rounding);

char *
fpdec_as_ascii_literal(const fpdec_t *fpdec, bool no_trailing_zeros);

int
fpdec_as_sign_coeff128_exp(fpdec_sign_t *sign, uint128_t *coeff, int64_t *exp,
                           const fpdec_t *fpdec);

// Basic arithmetic operations

error_t
fpdec_add(fpdec_t *z, const fpdec_t *x, const fpdec_t *y);

error_t
fpdec_sub(fpdec_t *z, const fpdec_t *x, const fpdec_t *y);

error_t
fpdec_mul(fpdec_t *z, const fpdec_t *x, const fpdec_t *y);

error_t
fpdec_div(fpdec_t *z, const fpdec_t *x, const fpdec_t *y, int prec_limit,
          enum FPDEC_ROUNDING_MODE rounding);

error_t
fpdec_divmod(fpdec_t *q, fpdec_t *r, const fpdec_t *x, const fpdec_t *y);

// Deallocator

void
fpdec_reset_to_zero(fpdec_t *fpdec, fpdec_dec_prec_t dec_prec);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // FPDEC_H
