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
#include "digit_array.h"
#include "mem.h"
#include "rounding.h"
#include "shifted_int.h"


/*****************************************************************************
*  Macros
*****************************************************************************/

// Limits

#define FPDEC_MAX_DEC_PREC UINT16_MAX
#define FPDEC_MIN_EXP -3450  // -FPDEC_MAX_DEC_PREC / DEC_DIGITS_PER_DIGIT + 1
#define FPDEC_MAX_EXP INT32_MAX

// Properties

#define FPDEC_IS_DYN_ALLOC(fpdec) (((fpdec_t*)fpdec)->dyn_alloc)

#define FPDEC_IS_NORMALIZED(fpdec) (((fpdec_t*)fpdec)->normalized)

#define FPDEC_EQ_ZERO(fpdec) (((fpdec_t*)fpdec)->sign == FPDEC_SIGN_ZERO)

#define FPDEC_IS_NEGATIVE(fpdec) (((fpdec_t*)fpdec)->sign == FPDEC_SIGN_NEG)

// Access to members

#define FPDEC_SIGN(fpdec) (((fpdec_t*)fpdec)->sign)

#define FPDEC_DEC_PREC(fpdec) (((fpdec_t*)fpdec)->dec_prec)

#define FPDEC_EXP(fpdec) \
        (FPDEC_IS_DYN_ALLOC(fpdec) ? ((fpdec_t*)fpdec)->exp : 0)

#define FPDEC_N_DIGITS(fpdec) \
        (FPDEC_IS_DYN_ALLOC(fpdec) ? \
            ((fpdec_t*)fpdec)->digit_array->n_signif : 2)

/*****************************************************************************
*  Constants
*****************************************************************************/

static const fpdec_t FPDEC_ZERO = {
        .dyn_alloc = false,
        .normalized = false,
        .sign = 0,
        .dec_prec = 0,
        .hi = 0,
        .lo = 0,
};
static const fpdec_t FPDEC_ONE = {
        .dyn_alloc = false,
        .normalized = false,
        .sign = 1,
        .dec_prec = 0,
        .hi = 0,
        .lo = 1,
};
static const fpdec_t FPDEC_MINUS_ONE = {
        .dyn_alloc = false,
        .normalized = false,
        .sign = -1,
        .dec_prec = 0,
        .hi = 0,
        .lo = 1,
};

/*****************************************************************************
*  Functions
*****************************************************************************/

// For testing only!

void
fpdec_dump(const fpdec_t *fpdec);

// Initializer

error_t
fpdec_copy(fpdec_t *fpdec, const fpdec_t *src);

error_t
fpdec_from_ascii_literal(fpdec_t *fpdec, const char *literal);

error_t
fpdec_from_long_long(fpdec_t *fpdec, long long val);

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
fpdec_adjusted(fpdec_t *fpdec, const fpdec_t *src, fpdec_dec_prec_t dec_prec,
               enum FPDEC_ROUNDING_MODE rounding);

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
