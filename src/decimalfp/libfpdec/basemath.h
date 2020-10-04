/* ---------------------------------------------------------------------------
Name:        basemath.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_BASEMATH_H
#define FPDEC_BASEMATH_H

#include <assert.h>

#include "helper_macros.h"

/*****************************************************************************
*  Macros
*****************************************************************************/

#define UINT64_10_POW_N_CUTOFF 19

#define UINT128_SET_MAX(x) {(x)->lo = UINT64_MAX; (x)->hi = UINT64_MAX;}
#define UINT128_CHECK_MAX(x) ((x)->lo == UINT64_MAX && (x)->hi == UINT64_MAX)
#define SIGNAL_OVERFLOW(x) UINT128_SET_MAX((x))

#define U64_HI(x) (((uint64_t)x) >> 32U)
#define U64_LO(x) (((uint64_t)x) & 0xFFFFFFFF)

/*****************************************************************************
*  Functions
*****************************************************************************/

// Comparison

static inline int
u128_cmp(const uint128_t *x, const uint128_t *y) {
    return ((x->hi > y->hi) || ((x->hi == y->hi) && (x->lo > y->lo))) -
            ((x->hi < y->hi) || ((x->hi == y->hi) && (x->lo < y->lo)));
}

// Addition

static inline void
u128_iadd_u64(uint128_t *x, const uint64_t y) {
    uint64_t t = x->lo + y;
    x->hi += (t < x->lo);
    x->lo = t;
}

static inline void
u128_incr(uint128_t *x) {
    u128_iadd_u64(x, 1UL);
}

static inline void
u128_iadd_u128(uint128_t *x, const uint128_t *y) {
    uint64_t t = x->lo + y->lo;
    x->hi += y->hi + (t < x->lo);
    x->lo = t;
}

// Subtraction

static inline void
u128_isub_u64(uint128_t *x, const uint64_t y) {
    uint64_t t = x->lo - y;
    x->hi -= (t > x->lo);
    x->lo = t;
}

static inline void
u128_decr(uint128_t *x) {
    u128_isub_u64(x, 1UL);
}

static inline void
u128_isub_u128(uint128_t *x, const uint128_t *y) {
    assert(u128_cmp(x, y) >= 0);
    uint64_t t = x->lo - y->lo;
    x->hi -= y->hi + (t > x->lo);
    x->lo = t;
}

static inline void
u128_sub_u128(uint128_t *z, const uint128_t *x, const uint128_t *y) {
    assert(u128_cmp(x, y) >= 0);
    uint64_t t = x->lo - y->lo;
    z->hi = x->hi - y->hi - (t > x->lo);
    z->lo = t;
}

// Multiplication

void
u64_mul_u64(uint128_t *z, uint64_t x, uint64_t y);

void
u128_imul_u64(uint128_t *x, uint64_t y);

static inline void
u128_imul_10_pow_n(uint128_t *x, const uint8_t n) {
    assert(n <= UINT64_10_POW_N_CUTOFF);
    if (x->hi == 0)
        u64_mul_u64(x, x->lo, _10_POW_N(n));
    else
        u128_imul_u64(x, _10_POW_N(n));
}

// Division

uint64_t
u128_idiv_u64(uint128_t *x, uint64_t y);

void
u128_idiv_u128(uint128_t *r, uint128_t *x, const uint128_t *y);

static inline uint64_t
u128_idiv_10(uint128_t *x) {
    uint64_t th, tl, r;
    th = U64_HI(x->hi);
    r = th % 10;
    tl = (r << 32U) + U64_LO(x->hi);
    x->hi = ((th / 10) << 32U) + tl / 10;
    r = tl % 10;
    th = (r << 32U) + U64_HI(x->lo);
    r = th % 10;
    tl = (r << 32U) + U64_LO(x->lo);
    x->lo = ((th / 10) << 32U) + tl / 10;
    return tl % 10;
}

#endif //FPDEC_BASEMATH_H
