/*
------------------------------------------------------------------------------
Name:        common.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_COMMON_H
#define FPDEC_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __SIZEOF_INT128__
#include "uint128_type_native.h"
#else
#include "uint128_type.h"
#endif // __int128


/*****************************************************************************
*  Common types
*****************************************************************************/

// error_t may or may not be available from errno.h
#ifndef __error_t_defined
#define __error_t_defined 1
typedef int error_t;
#endif

// sign indicator: =0 -> zero, <0 -> negative, >0 -> positive
typedef int8_t fpdec_sign_t;

// number of decimal fractional digits
typedef uint16_t fpdec_dec_prec_t;

// single decimal digit
typedef unsigned char dec_digit_t;

// single digit (base 2 ** 64 or 10 ** 19)
typedef uint64_t fpdec_digit_t;

// digit counter
typedef uint32_t fpdec_n_digits_t;

typedef int32_t fpdec_exp_t;

typedef struct fpdec_digit_array fpdec_digit_array_t;

typedef struct fpdec_struct fpdec_t;

/*****************************************************************************
*  Macros
*****************************************************************************/

#define DEC_DIGITS_PER_DIGIT 19             // int(log10(2^64))
#define RADIX 10000000000000000000UL        // 10 ** DEC_DIGITS_PER_DIGIT

// Limits
#define FPDEC_MAX_DEC_PREC UINT16_MAX
#define FPDEC_MIN_EXP -3450  // -FPDEC_MAX_DEC_PREC / DEC_DIGITS_PER_DIGIT + 1
#define FPDEC_MAX_EXP INT32_MAX

// Sign constants
#define FPDEC_SIGN_ZERO 0
#define FPDEC_SIGN_NEG -1
#define FPDEC_SIGN_POS 1

// Error codes
#define FPDEC_OK 0
#define FPDEC_PREC_LIMIT_EXCEEDED 1
#define FPDEC_EXP_LIMIT_EXCEEDED 2
#define FPDEC_N_DIGITS_LIMIT_EXCEEDED 3
#define FPDEC_INVALID_DECIMAL_LITERAL 4
#define FPDEC_DIVIDE_BY_ZERO 5
#define FPDEC_INVALID_FORMAT 6
#define FPDEC_INCOMPAT_LOCALE 7

#ifdef __cplusplus
}
#endif // __cplusplus

#endif //FPDEC_COMMON_H
