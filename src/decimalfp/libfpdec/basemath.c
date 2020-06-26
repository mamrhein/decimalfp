/* ---------------------------------------------------------------------------
Name:        basemath.c

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <assert.h>

#include "basemath.h"

/*****************************************************************************
*  Functions
*****************************************************************************/

// Bit arithmetic

static inline unsigned
u64_most_signif_bit_pos(uint64_t x) {
    unsigned n = 0;
    uint64_t t;

    for (unsigned shift = 32U; shift > 0; shift >>= 1) {
        t = x >> shift;
        if (t != 0) {
            n += shift;
            x = t;
        }
    }
    return n;
}

static inline unsigned
u128_n_signif_u32(const uint128_t *x) {
    return (x->hi != 0) ?
           (U64_HI(x->hi) != 0 ? 4 : 3) :
           (U64_HI(x->lo) != 0 ? 2 : 1);
}

static inline unsigned
u64_n_leading_0_bits(uint64_t x) {
    if (x == 0) return 64;
    return 63 - u64_most_signif_bit_pos(x);
}

// Multiplication

void
u64_mul_u64(uint128_t *z, const uint64_t x, const uint64_t y) {
    const uint64_t xl = U64_LO(x);
    const uint64_t xh = U64_HI(x);
    const uint64_t yl = U64_LO(y);
    const uint64_t yh = U64_HI(y);
    register uint64_t t;

    t = xl * yl;
    z->lo = U64_LO(t);
    t = xl * yh + U64_HI(t);
    z->hi = U64_HI(t);
    t = xh * yl + U64_LO(t);
    z->lo += (U64_LO(t) << 32U);
    z->hi += xh * yh + U64_HI(t);
}

void
u128_imul_u64(uint128_t *x, const uint64_t y) {
    uint128_t t;

    u64_mul_u64(&t, x->hi, y);
    if (t.hi != 0) {
        SIGNAL_OVERFLOW(x);
        return;
    }
    x->hi = t.lo;
    u64_mul_u64(&t, x->lo, y);
    x->hi += t.hi;
    if (x->hi < t.hi) {
        SIGNAL_OVERFLOW(x);
        return;
    }
    x->lo = t.lo;
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

    th = U64_HI(x->hi);
    r = th % y;
    tl = (r << 32U) + U64_LO(x->hi);
    x->hi = ((th / y) << 32U) + tl / y;
    r = tl % y;
    th = (r << 32U) + U64_HI(x->lo);
    r = th % y;
    tl = (r << 32U) + U64_LO(x->lo);
    x->lo = ((th / y) << 32U) + tl / y;
    return tl % y;
}

// Specialized version adapted from
// Henry S. Warren, Hacker’s Delight,
// originally found at http://www.hackersdelight.org/HDcode/divlu.c.txt.
// That code is in turn based on Algorithm D from
// D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// adapted to the special case m = 4 and n = 2 and x->hi < y (!).
// The link given above does not exist anymore, but the code can still be
// found at https://github.com/hcs0/Hackers-Delight/blob/master/divlu.c.txt.
static uint64_t
u128_idiv_u64_special(uint64_t *xlo, uint64_t *xhi, uint64_t y) {
    const uint64_t b = 1UL << 32U;
    unsigned n_bits;
    uint64_t xn0, xn1, xn10, xn32, yn0, yn1, q0, q1, t, rhat;

    assert(U64_HI(y) != 0);
    assert(*xhi < y);

    // Normalize dividend and divisor, so that y > 2^63 (i.e. highest bit set)
    n_bits = u64_n_leading_0_bits(y);
    y <<= n_bits;
    yn1 = U64_HI(y);
    yn0 = U64_LO(y);

    xn32 = (*xhi << n_bits) | (n_bits == 0 ? 0 : *xlo >> (64 - n_bits));
    xn10 = *xlo << n_bits;
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
    *xhi = 0;
    *xlo = q1 * b + q0;
    // Denormalize remainder
    return (t * b + xn0 - q0 * y) >> n_bits;
}

uint64_t
u128_idiv_u64(uint128_t *x, const uint64_t y) {
    uint64_t t;

    assert(y != 0);

    if (U64_HI(y) == 0)
        return u128_idiv_u32(x, U64_LO(y));

    if (x->hi < y)
        return u128_idiv_u64_special(&(x->lo), &(x->hi), y);

    t = x->hi % y;
    x->hi /= y;
    return u128_idiv_u64_special(&(x->lo), &t, y);
}

// The following code is based on Algorithm D from
// D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// adapted to base 2^64 and special case m = n = 2
static void
u128_idiv_u128_special(uint128_t *r, uint128_t *x, const uint128_t *y) {
    unsigned n_bits_left, n_bits_right;
    uint64_t xn[3], yn[2], q;
    uint128_t t = {0, 0};

    assert(y->hi != 0);
    assert(u128_cmp(x, y) >= 0);

    // Normalize dividend and divisor, so that y->hi > 2^63
    // (i.e. highest bit set)
    n_bits_left = u64_n_leading_0_bits(y->hi);   // n_bits_left < 64
    n_bits_right = 64 - n_bits_left;
    yn[0] = y->lo << n_bits_left;
    yn[1] = (y->hi << n_bits_left) + (y->lo >> n_bits_right);
    xn[0] = x->lo << n_bits_left;
    xn[1] = (x->hi << n_bits_left) + (x->lo >> n_bits_right);
    xn[2] = x->hi >> n_bits_right;

    // m = 2, n = 2
    // D2: Loop j not nessary because j = m - n = 0
    // D3: Calculate estimation of quotient
    // yn[1] > 2^63 and xn[2] < 2^63 => xn[2] < yn[1] => q < 2^64
    t.lo = xn[1];
    t.hi = xn[2];
    r->lo = u128_idiv_u64_special(&(t.lo), &(t.hi), yn[1]);
    assert(t.hi == 0);
    q = t.lo;
    // D4: Multiply and subtract
    t.lo = y->lo;
    t.hi = y->hi;
    u128_imul_u64(&t, q);
    u128_sub_u128(r, &t, x);
    // D5: Test remainder
    if (u128_cmp(r, y) <= 0) {
        x->lo = q;
        x->hi = 0;
    }
    else {
        // D6: Add back
        x->lo = ++q;
        x->hi = 0;
        u128_isub_u128(r, y);
    }
}

void
u128_idiv_u128(uint128_t *r, uint128_t *x, const uint128_t *y) {
    int cmp;

    assert(y->hi != 0);

    cmp = u128_cmp(x, y);

    // Special cases
    if (cmp == 0) {
        x->lo = 1;
        x->hi = 0;
        r->lo = 0;
        r->hi = 0;
    }
    else if (cmp < 0) {
        r->lo = x->lo;
        r->hi = x->hi;
        x->lo = 0;
        x->hi = 0;
    }
    else {
        u128_idiv_u128_special(r, x, y);
    }
}

