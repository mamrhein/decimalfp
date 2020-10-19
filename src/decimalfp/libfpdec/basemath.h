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
#include <stdint.h>

#include "common.h"
#include "uint64_math.h"
#ifdef __SIZEOF_INT128__
#include "uint128_math_native.h"
#else
#include "uint128_math.h"
#endif // __int128


/* Algorithm adopted from
 * Torbjörn Granlund and Peter L. Montgomery
 * Division by Invariant Integers using Multiplication
 * ACM SIGPLAN Notices, Vol. 29, No. 6
 *
 * The comments reference the formulas in the paper (translated to program
 * variables).
 *
 * N = 64
 * d = RADIX = 10^19
 * l =  1 + ⌊log₂ d⌋ = 64
 * m' = ⌊(2^N ∗ (2^l − d) − 1) / d⌋ = 15581492618384294730
 * Since N = l:
 * dnorm = d << (N - l) = d
 * n2 = (HIGH(n) << (N - l)) + (LOW(n) >> l) = hi
 * n10 = LOW(n) << (N - l) = lo
 */
static inline uint64_t
u128_idiv_radix_special(uint128_t *x) {
    static const uint64_t m = 15581492618384294730UL;
    uint128_t t = U128_RHS(m, 0);
    uint64_t lo = U128P_LO(x);
    uint64_t hi = U128P_HI(x);
    uint64_t neg_n1, n_adj, q1, q1_inv;

    // -n1 = XSIGN(n10) = n10 < 0 ? -1 : 0
    // neg_n1 = lo >= 2^63 ? UINT64_MAX : 0
    neg_n1 = -(lo >> 63U);
    // n_adj = n10 + AND(-n1, dnorm - 2^N)
    // i.e. n_adj = lo >= 2^63 ? lo + RADIX : RADIX
    n_adj = lo + (neg_n1 & RADIX);
    // Estimation of q:
    // q1 = n2 + HIGH(m' * (n2 - (-n1)) + n_adj)
    // First step:
    // t = m' * (n2 - (-n1))
    // i.e. t = lo >= 2^63 ? m' * (hi + 1) : m' * hi
    u128_imul_u64(&t, hi - neg_n1);
    // Second step:
    // t = t + n_adj
    u128_iadd_u64(&t, n_adj);
    // Third step:
    // q1 = hi + HIGH(t)
    q1 = hi + U128_HI(t);
    // Now we have (see Lemma 8.1)
    // 0 <= q1 <= 2^N - 1
    // and
    // 0 <= n - q1 * d < 2 * d
    // or, in program terms
    // 0 <= q1 <= UINT64_MAX
    // and
    // 0 <= x - q1 * RADIX < 2 * RADIX
    // Thus, our upper estimation of r is n - q1 * d, i.e x - q1 * RADIX.
    // The lower estimation of r can be calculated as
    // dr = n - q1 * d - d, or modulo int128
    // dr = n - 2^N * d + (2^N - 1 - q1) * d
    // Then
    // q = HIGH(dr) − (2^N − 1 − q1) + 2^N
    // r = LOW(dr) + AND(d − 2^N, HIGH(dr))
    // In program terms:
    // dr = x - q1 * RADIX - RADIX
    // dr = x - 2^64 * RADIX + (UINT64_MAX - q1) * RADIX
    // q = U128_HI(dr) − (UINT64_MAX - q1)
    // r = LOW(dr) + (RADIX & U128_HI(dr))

    // With some short cuts:
    q1_inv = UINT64_MAX - q1;
    u64_mul_u64(&t, q1_inv, RADIX);
    u128_iadd_u128(&t, x);
    hi = U128_HI(t) - RADIX;
    U128_FROM_LO_HI(x, hi - q1_inv, 0);
    return U128_LO(t) + (RADIX & hi);
}

static inline uint64_t
u128_idiv_radix(uint128_t *x) {
    uint64_t lo = U128P_LO(x);
    uint64_t hi = U128P_HI(x);

    if (hi == 0) {
        if (lo < RADIX) {
            U128_FROM_LO_HI(x, 0, 0);
            return lo;
        }
        else {
            U128_FROM_LO_HI(x, 1, 0);
            return lo - RADIX;
        }
    }
    else
        return u128_idiv_radix_special(x);
}

#endif //FPDEC_BASEMATH_H
