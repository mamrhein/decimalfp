/* ---------------------------------------------------------------------------
Name:        uint128_math.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_UINT128_MATH_H
#define FPDEC_UINT128_MATH_H

#include <assert.h>
#include <stdint.h>
#include "uint128_type.h"
#include "uint64_math.h"

/*****************************************************************************
*  Macros
*****************************************************************************/

// byte assignment
#define U128_RHS(lo, hi) {(lo), (hi)}
#define U128_FROM_LO_HI(ui, l, h) do {((uint128_t *)ui)->lo = (l); \
                                      ((uint128_t *)ui)->hi = (h);} while (0)

// byte selection
#define U128_HI(x) (((uint128_t)x).hi)
#define U128_LO(x) (((uint128_t)x).lo)
#define U128P_HI(x) (((uint128_t *)x)->hi)
#define U128P_LO(x) (((uint128_t *)x)->lo)

// overflow handling
#define SIGNAL_OVERFLOW(x) *x = UINT128_MAX
#define UINT128_CHECK_MAX(x) (U128P_LO(x) == UINT64_MAX && \
                              U128P_HI(x) == UINT64_MAX)

/*****************************************************************************
*  Functions
*****************************************************************************/

// Bit arithmetic

static inline unsigned
u128_n_signif_u32(const uint128_t x) {
    return (U128_HI(x) != 0) ?
           (U64_HI(U128_HI(x)) != 0 ? 4 : 3) :
           (U64_HI(U128_LO(x)) != 0 ? 2 : 1);
}

// Comparison

static inline int
u128_gt(const uint128_t x, const uint128_t y) {
    return (U128_HI(x) > U128_HI(y) ||
            U128_HI(x) == U128_HI(y) && U128_LO(x) > U128_LO(y));
}

static inline int
u128_lt(const uint128_t x, const uint128_t y) {
    return (U128_HI(x) < U128_HI(y) ||
            U128_HI(x) == U128_HI(y) && U128_LO(x) < U128_LO(y));
}

static inline int
u128_cmp(const uint128_t x, const uint128_t y) {
    return u128_gt(x, y) - u128_lt(x, y);
}

// Addition

static inline void
u128_iadd_u64(uint128_t *x, const uint64_t y) {
    register uint64_t t = U128P_LO(x) + y;
    U128P_HI(x) += (t < U128P_LO(x));
    U128P_LO(x) = t;
}

static inline void
u128_incr(uint128_t *x) {
    U128P_LO(x) += 1UL;
    U128P_HI(x) += (U128P_LO(x) == 0UL);
}

static inline void
u128_iadd_u128(uint128_t *x, const uint128_t *y) {
    register uint64_t t = U128P_LO(x) + U128P_LO(y);
    U128P_HI(x) += U128P_HI(y) + (t < U128P_LO(x));
    U128P_LO(x) = t;
}

// Subtraction

static inline void
u128_isub_u64(uint128_t *x, const uint64_t y) {
    register uint64_t t = U128P_LO(x) - y;
    U128P_HI(x) -= (t > U128P_LO(x));
    U128P_LO(x) = t;
}

static inline void
u128_decr(uint128_t *x) {
    U128P_HI(x) -= (U128P_LO(x) == 0UL);
    U128P_LO(x) -= 1UL;
}

static inline void
u128_isub_u128(uint128_t *x, const uint128_t *y) {
    assert(u128_cmp(*x, *y) >= 0);
    register uint64_t t = U128P_LO(x) - U128P_LO(y);
    U128P_HI(x) -= U128P_HI(y) + (t > U128P_LO(x));
    U128P_LO(x) = t;
}

static inline void
u128_sub_u128(uint128_t *z, const uint128_t *x, const uint128_t *y) {
    assert(u128_cmp(*x, *y) >= 0);
    register uint64_t t = U128P_LO(x) - U128P_LO(y);
    U128P_HI(z) = U128P_HI(x) - U128P_HI(y) - (t > U128P_LO(x));
    U128P_LO(z) = t;
}

// Multiplication

static void
u64_mul_u64(uint128_t *z, const uint64_t x, const uint64_t y) {
    const uint64_t xl = U64_LO(x);
    const uint64_t xh = U64_HI(x);
    const uint64_t yl = U64_LO(y);
    const uint64_t yh = U64_HI(y);
    register uint64_t t;

    t = xl * yl;
    U128P_LO(z) = U64_LO(t);
    t = xl * yh + U64_HI(t);
    U128P_HI(z) = U64_HI(t);
    t = xh * yl + U64_LO(t);
    U128P_LO(z) += (U64_LO(t) << 32U);
    U128P_HI(z) += xh * yh + U64_HI(t);
}

static void
u128_imul_u64(uint128_t *x, const uint64_t y) {
    uint128_t t;

    u64_mul_u64(&t, U128P_HI(x), y);
    if (U128_HI(t) != 0) {
        SIGNAL_OVERFLOW(x);
        return;
    }
    U128P_HI(x) = U128_LO(t);
    u64_mul_u64(&t, U128P_LO(x), y);
    U128P_HI(x) += U128_HI(t);
    if (U128P_HI(x) < U128_HI(t)) {
        SIGNAL_OVERFLOW(x);
        return;
    }
    U128P_LO(x) = U128_LO(t);
}

static inline void
u128_imul_10_pow_n(uint128_t *x, const uint8_t n) {
    if (U128P_HI(x) == 0)
        u64_mul_u64(x, U128P_LO(x), u64_10_pow_n(n));
    else
        u128_imul_u64(x, u64_10_pow_n(n));
}

// Division

// adapted from
// D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// Exercise 16
static uint64_t
u128_idiv_u32(uint128_t *x, uint32_t y) {
    uint64_t th, tl, r;

    assert(y != 0);

    if (y == 1) return 0UL;

    th = U64_HI(U128P_HI(x));
    r = th % y;
    tl = (r << 32U) + U64_LO(U128P_HI(x));
    U128P_HI(x) = ((th / y) << 32U) + tl / y;
    r = tl % y;
    th = (r << 32U) + U64_HI(U128P_LO(x));
    r = th % y;
    tl = (r << 32U) + U64_LO(U128P_LO(x));
    U128P_LO(x) = ((th / y) << 32U) + tl / y;
    return tl % y;
}

// Specialized version adapted from
// Henry S. Warren, Hacker’s Delight,
// originally found at http://www.hackersdelight.org/HDcode/divlu.c.txt.
// That code is in turn based on Algorithm D from
// D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// adapted to the special case m = 4 and n = 2 and U128P_HI(x) < y (!).
// The link given above does not exist anymore, but the code can still be
// found at https://github.com/hcs0/Hackers-Delight/blob/master/divlu.c.txt.
static uint64_t
u128_idiv_u64_special(uint128_t *x, uint64_t y) {
    const uint64_t b = 1UL << 32U;
    unsigned n_bits;
    uint64_t xn0, xn1, xn10, xn32, yn0, yn1, q0, q1, t, rhat;

    assert(U64_HI(y) != 0);
    assert(U128P_HI(x) < y);

    // Normalize dividend and divisor, so that y > 2^63 (i.e. highest bit set)
    n_bits = u64_n_leading_0_bits(y);
    y <<= n_bits;
    yn1 = U64_HI(y);
    yn0 = U64_LO(y);

    xn32 = (U128P_HI(x) << n_bits) |
           (n_bits == 0 ? 0 : U128P_LO(x) >> (64 - n_bits));
    xn10 = U128P_LO(x) << n_bits;
    xn0 = U64_LO(xn10);
    xn1 = U64_HI(xn10);

    q1 = xn32 / yn1;
    rhat = xn32 % yn1;
    // Now we have
    // q1 * yn1 + rhat = xn32
    // so that
    // q1 * yn1 * 2^32 + rhat * 2^32 + xn1 = xn32 * 2^32 + xn1
    while (q1 >= b || q1 * yn0 > rhat * b + xn1) {
        q1--;
        rhat += yn1;
        if (rhat >= b)
            break;
    }
    // The loop did not change the equation given above. It was terminated if
    // either q1 < 2^32 or rhat >= 2^32 or q1 * yn0 > rhat * 2^32 + xn1.
    // In these cases follows:
    // q1 * yn0 <= rhat * 2^32 + xn1, therefor
    // q1 * yn1 * 2^32 + q1 * yn0 <= xn32 * 2^32 + xn1, and
    // q1 * y <= xn32 * 2^32 + xn1, and
    // xn32 * 2^32 + xn1 - q1 * y >= 0.
    // That means that the add-back step in Knuth's algorithm is not required.

    // Since the final quotient is < 2^64, this must also be true for
    // xn32 * 2^32 + xn1 - q1 * y. Thus, in the following we can safely
    // ignore any possible overflow in xn32 * 2^32 or q1 * y.
    t = xn32 * b + xn1 - q1 * y;
    q0 = t / yn1;
    rhat = t % yn1;
    while (q0 >= b || q0 * yn0 > rhat * b + xn0) {
        q0--;
        rhat += yn1;
        if (rhat >= b)
            break;
    }
    // Write back result
    U128P_HI(x) = 0;
    U128P_LO(x) = q1 * b + q0;
    // Denormalize remainder
    return (t * b + xn0 - q0 * y) >> n_bits;
}

static uint64_t
u128_idiv_u64(uint128_t *x, const uint64_t y) {
    uint64_t r;
    uint128_t t;

    assert(y != 0);

    if (U64_HI(y) == 0)
        return u128_idiv_u32(x, U64_LO(y));

    if (U128P_HI(x) < y)
        return u128_idiv_u64_special(x, y);

    U128P_HI(&t) = U128P_HI(x) % y;
    U128P_LO(&t) = U128P_LO(x);
    r = u128_idiv_u64_special(&t, y);
    U128P_HI(x) /= y;
    U128P_LO(x) = U128_LO(t);
    return r;
}

// The following code is based on Algorithm D from
// D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// adapted to base 2^64 and special case m = n = 2
static void
u128_idiv_u128_special(uint128_t *r, uint128_t *x, const uint128_t *y) {
    unsigned n_bits_left, n_bits_right;
    uint64_t xn[3], yn[2], q;
    uint128_t t = {0, 0};

    assert(U128P_HI(y) != 0);
    assert(u128_cmp(*x, *y) >= 0);

    // Normalize dividend and divisor, so that U128P_HI(y) > 2^63
    // (i.e. highest bit set)
    n_bits_left = u64_n_leading_0_bits(U128P_HI(y));   // n_bits_left < 64
    n_bits_right = 64 - n_bits_left;
    yn[0] = U128P_LO(y) << n_bits_left;
    yn[1] = (U128P_HI(y) << n_bits_left) + (U128P_LO(y) >> n_bits_right);
    xn[0] = U128P_LO(x) << n_bits_left;
    xn[1] = (U128P_HI(x) << n_bits_left) + (U128P_LO(x) >> n_bits_right);
    xn[2] = U128P_HI(x) >> n_bits_right;

    // m = 2, n = 2
    // D2: Loop j not nessary because j = m - n = 0
    // D3: Calculate estimation of quotient
    // yn[1] > 2^63 and xn[2] < 2^63 => xn[2] < yn[1] => q < 2^64
    U128P_LO(&t) = xn[1];
    U128P_HI(&t) = xn[2];
    r->lo = u128_idiv_u64_special(&t, yn[1]);
    assert(U128_HI(t) == 0);
    q = U128_LO(t);
    // D4: Multiply q * y
    U128P_LO(&t) = U128P_LO(y);
    U128P_HI(&t) = U128P_HI(y);
    u128_imul_u64(&t, q);
    // D5: Test q * y against x
    if (u128_cmp(t, *x) <= 0) {
        u128_sub_u128(r, x, &t);
        U128P_LO(x) = q;
        U128P_HI(x) = 0;
    }
    else {
        u128_isub_u128(&t, y);
        u128_sub_u128(r, x, &t);
        U128P_LO(x) = q - 1;
        U128P_HI(x) = 0;
    }
}

static void
u128_idiv_u128(uint128_t *r, uint128_t *x, const uint128_t *y) {
    int cmp;

    assert(U128P_HI(y) != 0);

    cmp = u128_cmp(*x, *y);

    // Special cases
    if (cmp == 0) {
        U128P_LO(x) = 1;
        U128P_HI(x) = 0;
        r->lo = 0;
        r->hi = 0;
    }
    else if (cmp < 0) {
        r->lo = U128P_LO(x);
        r->hi = U128P_HI(x);
        U128P_LO(x) = 0;
        U128P_HI(x) = 0;
    }
    else {
        u128_idiv_u128_special(r, x, y);
    }
}

static inline uint64_t
u128_idiv_10(uint128_t *x) {
    uint64_t th, tl, r;
    th = U64_HI(U128P_HI(x));
    r = th % 10;
    tl = (r << 32U) + U64_LO(U128P_HI(x));
    U128P_HI(x) = ((th / 10) << 32U) + tl / 10;
    r = tl % 10;
    th = (r << 32U) + U64_HI(U128P_LO(x));
    r = th % 10;
    tl = (r << 32U) + U64_LO(U128P_LO(x));
    U128P_LO(x) = ((th / 10) << 32U) + tl / 10;
    return tl % 10;
}

#endif // FPDEC_UINT128_MATH_H
