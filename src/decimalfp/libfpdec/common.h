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

// single digit (base 2 ** 64 or 10 ** 19)
typedef uint64_t fpdec_digit_t;

// digit counter
typedef uint32_t fpdec_n_digits_t;

// large unsigned int
typedef struct {
    uint64_t lo;
    uint64_t hi;
} uint128_t;

typedef int32_t fpdec_exp_t;

//forward decl of digit array
struct fpdec_digit_array;
typedef struct fpdec_digit_array fpdec_digit_array_t;

typedef struct {
    bool dyn_alloc: 1;           // true indicates digit array
    bool normalized: 1;          // true if digit array is normalized
    fpdec_sign_t sign;          // sign indicator
    fpdec_dec_prec_t dec_prec;  // number of decimal fractional digits
    //                             variants:
    union {                     // shifted int          digit_array
        uint32_t hi;            // high 32 bits
        fpdec_exp_t exp;        //                      exponent (base 2**64)
    };
    union {
        fpdec_digit_t lo;       // low  64 bits
        fpdec_digit_array_t *digit_array;   //          pointer to digit array
    };
} fpdec_t;

/*****************************************************************************
*  Macros
*****************************************************************************/

// sign
#define FPDEC_SIGN_ZERO 0
#define FPDEC_SIGN_NEG -1
#define FPDEC_SIGN_POS 1

// error codes
#define FPDEC_OK 0
#define FPDEC_PREC_LIMIT_EXCEEDED 1
#define FPDEC_EXP_LIMIT_EXCEEDED 2
#define FPDEC_N_DIGITS_LIMIT_EXCEEDED 3
#define FPDEC_INVALID_DECIMAL_LITERAL 4
#define FPDEC_DIVIDE_BY_ZERO 5

#ifdef __cplusplus
}
#endif // __cplusplus

#endif //FPDEC_COMMON_H
